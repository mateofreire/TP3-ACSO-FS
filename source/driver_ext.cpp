#include "all_heads.h"


/********************************
 *				*
 *	 Clase TDriverEXT	*
 *				*
 ********************************/
/****************************************************************************************************************************************
 *																	*
 *							TDriverEXT :: TDriverEXT							*
 *																	*
 * OBJETIVO: Inicializar la clase recién creada.											*
 *																	*
 * ENTRADA: DiskData: Puntero a un bloque de memoria con la imágen del disco a analizar.						*
 *	    LongitudDiskData: Tamaño, en bytes, de la imágen a analizar.								*
 *																	*
 * SALIDA: Nada.															*
 *																	*
 ****************************************************************************************************************************************/
TDriverEXT::TDriverEXT(const unsigned char *DiskData, unsigned LongitudDiskData) : TDriverBase(DiskData, LongitudDiskData)
{
}


/****************************************************************************************************************************************
 *																	*
 *							TDriverEXT :: ~TDriverEXT							*
 *																	*
 * OBJETIVO: Liberar recursos alocados.													*
 *																	*
 * ENTRADA: Nada.															*
 *																	*
 * SALIDA: Nada.															*
 *																	*
 ****************************************************************************************************************************************/
TDriverEXT::~TDriverEXT()
{
}

/****************************************************************************************************************************************
 *																	*
 *						   TDriverEXT :: LevantarDatosSuperbloque						*
 *																	*
 * OBJETIVO: Esta función analiza el superbloque y completa la estructura DatosFS con los datos levantados.				*
 *																	*
 * ENTRADA: Nada.															*
 *																	*
 * SALIDA: En el nombre de la función CODERROR_NINGUNO si no hubo errores. Sino uno de los siguientes valores:				*
 *		CODERROR_SUPERBLOQUE_INVALIDO   : El superbloque está dañado o no corresponde a un disco con ningún formato.		*
 *		CODERROR_FILESYSTEM_DESCONOCIDO : El superbloque es válido, pero no corresponde a un FyleSystem soportado por esta	*
 *						  clase.										*
 * ****************************************************************************************************************************************/
int TDriverEXT::LevantarDatosSuperbloque()
{
/* ACORDARSE QUE HARDCODEAMOS EL PeriodoAgrupadoFlex */
/* ACORDARSE QUE NOS DIO MAL CON LA REF EL nro de clusters reservado GDT */
/* Para poder usar PunteroASector() más allá del sector 0 necesitamos fijar BytesPorSector.
	   EXT2/3/4 usan típicamente 512 B/sector, así que partimos de 512. */
	DatosFS.BytesPorSector = 512;

	/* El superbloque está a 1024 bytes desde el inicio ⇒ sector lógico 2. */
	const unsigned char *sb = PunteroASector(2);
	if (!sb)
		return CODERROR_SUPERBLOQUE_INVALIDO;

	/* Helpers de lectura (valores little-endian en disco). */
    #define RD16(off)  ((unsigned short)(sb[off] | (sb[(off)+1] << 8)))
    #define RD32(off)  ((unsigned int)(sb[off] | (sb[(off)+1] << 8) | (sb[(off)+2] << 16) | (sb[(off)+3] << 24)))

	/* Validar firma del superbloque (offset 0x38 dentro del superbloque). */
	if (RD16(0x38) != 0xEF53)
		return CODERROR_SUPERBLOQUE_INVALIDO;

	/* Leer campos básicos EXT2 del superbloque: */
	unsigned s_inodes_count      = RD32(0x00);
	unsigned s_blocks_count_lo   = RD32(0x04);
	unsigned s_log_block_size    = RD32(0x18);
	unsigned s_blocks_per_group  = RD32(0x20);
	unsigned s_inodes_per_group  = RD32(0x28);
	unsigned s_inode_size        = RD16(0x58);
	unsigned s_feat_compat       = RD32(0x5C);
	unsigned s_feat_incompat     = RD32(0x60);
	unsigned s_feat_ro_compat    = RD32(0x64);
    unsigned s_r_blocks_count    = RD16(0xCE);

	/* Esta cátedra usa “Cluster” ~ “Block” en EXT: BlockSize = 1024 << s_log_block_size. */
	DatosFS.TipoFilesystem               = tfsEXT2;
	DatosFS.BytesPorCluster              = 1024u << s_log_block_size;
	DatosFS.NumeroDeClusters             = s_blocks_count_lo;

	/* Campos específicos EXT que pide el enunciado: */
	DatosFS.DatosEspecificos.EXT.CaracteristicasCompatibles   = (int)s_feat_compat;
	DatosFS.DatosEspecificos.EXT.CaracteristicasIncompatibles = (int)s_feat_incompat;
	DatosFS.DatosEspecificos.EXT.CaracteristicasSoloLectura   = (int)s_feat_ro_compat;
	DatosFS.DatosEspecificos.EXT.NumeroDeINodes               = (int)s_inodes_count;
    DatosFS.DatosEspecificos.EXT.ClustersReservadosGDT        = (int)s_r_blocks_count;
	DatosFS.DatosEspecificos.EXT.ClustersPorGrupo             = (int)s_blocks_per_group;
	DatosFS.DatosEspecificos.EXT.INodesPorGrupo               = (int)s_inodes_per_group;
	DatosFS.DatosEspecificos.EXT.BytesPorINode                = (int)s_inode_size;

	/* EXT2 puro: estos dos suelen ser 0; se piden en la estructura, los dejamos en 0. */
	DatosFS.DatosEspecificos.EXT.PeriodoAgrupadoFlex = 0;

	/* Derivados: número de grupos. */
	if (DatosFS.DatosEspecificos.EXT.ClustersPorGrupo == 0)
		return CODERROR_SUPERBLOQUE_INVALIDO;

	DatosFS.DatosEspecificos.EXT.NroGrupos = DatosFS.NumeroDeClusters / DatosFS.DatosEspecificos.EXT.ClustersPorGrupo;

	/* Para EXT2: leer la Tabla de Descriptores de Grupo (GDT) con TEntradaDescGrupoEXT23
	   y completar DatosGrupo. */
	{
		unsigned desc_size = sizeof(TEntradaDescGrupoEXT23);
		unsigned sectors_per_cluster = DatosFS.BytesPorCluster / DatosFS.BytesPorSector;

		/* En EXT2 la GDT comienza en el bloque 2 si el tamaño de bloque es 1024, sino en el bloque 1. */
		unsigned gd_start_block = (DatosFS.BytesPorCluster == 1024) ? 2 : 1;

		unsigned desc_per_block = DatosFS.BytesPorCluster / desc_size;

		/* Preparar vector */
		DatosFS.DatosEspecificos.EXT.DatosGrupo.clear();
		DatosFS.DatosEspecificos.EXT.DatosGrupo.resize(DatosFS.DatosEspecificos.EXT.NroGrupos);

		for (int i = 0; i < DatosFS.DatosEspecificos.EXT.NroGrupos; i++)
		{
			unsigned block_index = gd_start_block + (i / desc_per_block);
			unsigned sector = block_index * sectors_per_cluster;

			const unsigned char *pblock = PunteroASector(sector);
			if (!pblock)
				return CODERROR_SUPERBLOQUE_INVALIDO;

			unsigned offset_in_block = (i % desc_per_block) * desc_size;
			const unsigned char *p = pblock + offset_in_block;

			/* Lectura little-endian desde p */
			#define RD32P(o) ((unsigned int)(p[(o)] | (p[(o)+1] << 8) | (p[(o)+2] << 16) | (p[(o)+3] << 24)))

			unsigned block_bitmap = RD32P(0);
			unsigned inode_bitmap = RD32P(4);
			unsigned inode_table  = RD32P(8);

			DatosFS.DatosEspecificos.EXT.DatosGrupo[i].ClusterBitmapINodes = (unsigned long long)inode_bitmap;
			DatosFS.DatosEspecificos.EXT.DatosGrupo[i].ClusterBitmapBloques = (unsigned long long)block_bitmap;
			DatosFS.DatosEspecificos.EXT.DatosGrupo[i].ClusterTablaINodes = (unsigned long long)inode_table;

			/* Calcular el primer bloque de datos del grupo: justo después de la tabla de inodos.
			   Número de bloques usados por la tabla de inodos = ceil(INodesPorGrupo * BytesPorINode / BytesPorCluster) */
			{
				unsigned long long inodes_per_group = (unsigned long long)DatosFS.DatosEspecificos.EXT.INodesPorGrupo;
				unsigned long long bytes_per_inode = (unsigned long long)DatosFS.DatosEspecificos.EXT.BytesPorINode;
				unsigned long long bytes_per_cluster = (unsigned long long)DatosFS.BytesPorCluster;

				unsigned long long inode_table_blocks = (inodes_per_group * bytes_per_inode + bytes_per_cluster - 1) / bytes_per_cluster;
				DatosFS.DatosEspecificos.EXT.DatosGrupo[i].ClusterTablaBloques = (unsigned long long)inode_table + inode_table_blocks;
			}

			#undef RD32P
		}
	}

	return CODERROR_NINGUNO;
}

/****************************************************************************************************************************************
 *																	*
 *						  TDriverEXT :: ListarDirectorio							*
 *																	*
 * OBJETIVO: Esta función enumera las entradas en un directorio y retorna un arreglo de elementos, uno por cada entrada.		*
 *																	*
 * ENTRADA: Path: Path al directorio enumerar (cadena de nombres de directorio separados por '/').					*
 *																	*
 * SALIDA: En el nombre de la función CODERROR_NINGUNO si no hubo errores, caso contrario el código de error.				*
 *	   Entradas: Arreglo con cada una de las entradas.										*
 *																	*
 ****************************************************************************************************************************************/
int TDriverEXT::ListarDirectorio(const char *Path, std::vector<TEntradaDirectorio> &Entradas)
{
	/* Implementación exclusivamente para EXT2. No usa lambdas ni auto. */
	Entradas.clear();

	if (!Path)
		return CODERROR_PARAMETROS_INVALIDOS;

	/* Ruta debe ser absoluta */
	if (Path[0] != '/')
		return CODERROR_RUTA_NO_ABSOLUTA;

	if (DatosFS.TipoFilesystem != tfsEXT2)
		return CODERROR_FILESYSTEM_DESCONOCIDO;

	unsigned sectores_por_cluster = DatosFS.BytesPorCluster / DatosFS.BytesPorSector;

	/* Shortcut variables from DatosFS to avoid repetir accesos */
	unsigned inodes_por_grupo = (unsigned)DatosFS.DatosEspecificos.EXT.INodesPorGrupo;
	unsigned bytes_por_inode = (unsigned)DatosFS.DatosEspecificos.EXT.BytesPorINode;
	unsigned nro_grupos = (unsigned)DatosFS.DatosEspecificos.EXT.NroGrupos;

	/* Funcionamiento: resolver la ruta componente a componente, empezando en la raiz (inode 2) */
	unsigned current_inode = EXT_ROOT_INO; /* 2 */
	std::string ruta(Path);

	if (ruta != "/")
	{
		size_t pos = 1; /* saltar primer slash */
		while (pos < ruta.size())
		{
			size_t next = ruta.find('/', pos);
			std::string componente = (next==std::string::npos) ? ruta.substr(pos) : ruta.substr(pos, next-pos);
			if (componente.empty())
			{
				pos = (next==std::string::npos)? ruta.size() : next+1;
				continue;
			}

			/* Leer inode del directorio actual (current_inode) */
			if (current_inode == 0 || current_inode > (unsigned)DatosFS.DatosEspecificos.EXT.NumeroDeINodes)
				return CODERROR_DIRECTORIO_INEXISTENTE;

			unsigned grupo = (current_inode - 1) / inodes_por_grupo;
			if (grupo >= nro_grupos)
				return CODERROR_DIRECTORIO_INEXISTENTE;

			unsigned long long tabla_inodos = DatosFS.DatosEspecificos.EXT.DatosGrupo[grupo].ClusterTablaINodes;
			unsigned index_in_group = (current_inode - 1) % inodes_por_grupo;
			unsigned offset_in_table = index_in_group * bytes_por_inode;

			unsigned block_offset = offset_in_table / DatosFS.BytesPorCluster;
			unsigned offset_in_block = offset_in_table % DatosFS.BytesPorCluster;

			unsigned block = (unsigned)(tabla_inodos + block_offset);
			const unsigned char *pblock = PunteroASector((__u64)block * sectores_por_cluster);
			if (!pblock)
				return CODERROR_LECTURA_DISCO;

			TINodeEXT inode_dir;
			memset(&inode_dir, 0, sizeof(inode_dir));

			/* Copiar el inode, manejando cruce de cluster si es necesario */
			if (offset_in_block + bytes_por_inode <= (unsigned)DatosFS.BytesPorCluster)
			{
				memcpy(&inode_dir, pblock + offset_in_block, (bytes_por_inode < sizeof(TINodeEXT)) ? bytes_por_inode : sizeof(TINodeEXT));
			}
			else
			{
				/* Leer en dos partes */
				unsigned first_chunk = (unsigned)DatosFS.BytesPorCluster - offset_in_block;
				unsigned to_copy = (bytes_por_inode < sizeof(TINodeEXT)) ? bytes_por_inode : sizeof(TINodeEXT);
				unsigned copied = 0;
				if (first_chunk > 0)
				{
					unsigned c = (first_chunk < to_copy) ? first_chunk : to_copy;
					memcpy(&((unsigned char*)&inode_dir)[0], pblock + offset_in_block, c);
					copied += c;
				}
				if (copied < to_copy)
				{
					/* leer siguiente bloque de la tabla de inodos */
					const unsigned char *pblock2 = PunteroASector((__u64)(block + 1) * sectores_por_cluster);
					if (!pblock2)
						return CODERROR_LECTURA_DISCO;
					memcpy(&((unsigned char*)&inode_dir)[copied], pblock2, to_copy - copied);
				}
			}

			/* Verificar que sea directorio */
			if (!S_ISDIR(inode_dir.i_mode))
				return CODERROR_DIRECTORIO_INEXISTENTE;

			/* Buscar el componente dentro de las entradas del directorio */
			bool found = false;
			for (int bi = 0; bi < 12 && !found; bi++)
			{
				unsigned data_block = (unsigned)inode_dir.i_block[bi];
				if (data_block == 0)
					continue;

				const unsigned char *db = PunteroASector((__u64)data_block * sectores_por_cluster);
				if (!db)
					return CODERROR_LECTURA_DISCO;

				unsigned off = 0;
				while (off < (unsigned)DatosFS.BytesPorCluster)
				{
					const unsigned char *entry = db + off;
					unsigned inode_entry = entry[0] | (entry[1] << 8) | (entry[2] << 16) | (entry[3] << 24);
					unsigned rec_len = entry[4] | (entry[5] << 8);
					unsigned name_len = entry[6];

					if (rec_len == 0)
						break;

					if (inode_entry != 0 && name_len > 0 && name_len < rec_len)
					{
						std::string name((const char *)(entry + 8), name_len);
						if (name == componente)
						{
							current_inode = inode_entry;
							found = true;
							break;
						}
					}

					off += rec_len;
				}
			}

			if (!found)
				return CODERROR_DIRECTORIO_INEXISTENTE;

			pos = (next==std::string::npos)? ruta.size() : next+1;
		}
	}

	/* Ahora current_inode es el inode del directorio a listar: leerlo y listar sus entradas */
	if (current_inode == 0 || current_inode > (unsigned)DatosFS.DatosEspecificos.EXT.NumeroDeINodes)
		return CODERROR_DIRECTORIO_INEXISTENTE;

	unsigned grupo_root = (current_inode - 1) / inodes_por_grupo;
	if (grupo_root >= nro_grupos)
		return CODERROR_DIRECTORIO_INEXISTENTE;

	unsigned long long tabla_inodos_root = DatosFS.DatosEspecificos.EXT.DatosGrupo[grupo_root].ClusterTablaINodes;
	unsigned index_in_group_root = (current_inode - 1) % inodes_por_grupo;
	unsigned offset_in_table_root = index_in_group_root * bytes_por_inode;
	unsigned block_offset_root = offset_in_table_root / DatosFS.BytesPorCluster;
	unsigned offset_in_block_root = offset_in_table_root % DatosFS.BytesPorCluster;
	unsigned block_root = (unsigned)(tabla_inodos_root + block_offset_root);

	const unsigned char *pblock_root = PunteroASector((__u64)block_root * sectores_por_cluster);
	if (!pblock_root)
		return CODERROR_LECTURA_DISCO;

	TINodeEXT inode_dir_root;
	memset(&inode_dir_root, 0, sizeof(inode_dir_root));
	if (offset_in_block_root + bytes_por_inode <= (unsigned)DatosFS.BytesPorCluster)
	{
		memcpy(&inode_dir_root, pblock_root + offset_in_block_root, (bytes_por_inode < sizeof(TINodeEXT)) ? bytes_por_inode : sizeof(TINodeEXT));
	}
	else
	{
		unsigned first_chunk = (unsigned)DatosFS.BytesPorCluster - offset_in_block_root;
		unsigned to_copy = (bytes_por_inode < sizeof(TINodeEXT)) ? bytes_por_inode : sizeof(TINodeEXT);
		unsigned copied = 0;
		if (first_chunk > 0)
		{
			unsigned c = (first_chunk < to_copy) ? first_chunk : to_copy;
			memcpy(&((unsigned char*)&inode_dir_root)[0], pblock_root + offset_in_block_root, c);
			copied += c;
		}
		if (copied < to_copy)
		{
			const unsigned char *pblock2 = PunteroASector((__u64)(block_root + 1) * sectores_por_cluster);
			if (!pblock2)
				return CODERROR_LECTURA_DISCO;
			memcpy(&((unsigned char*)&inode_dir_root)[copied], pblock2, to_copy - copied);
		}
	}

	if (!S_ISDIR(inode_dir_root.i_mode))
		return CODERROR_DIRECTORIO_INEXISTENTE;

	/* Recorremos sus bloques directos y añadimos cada entrada a Entradas */
	for (int bi = 0; bi < 12; bi++)
	{
		unsigned data_block = (unsigned)inode_dir_root.i_block[bi];
		if (data_block == 0)
			continue;

		const unsigned char *db = PunteroASector((__u64)data_block * sectores_por_cluster);
		if (!db)
			return CODERROR_LECTURA_DISCO;

		unsigned off = 0;
		while (off < (unsigned)DatosFS.BytesPorCluster)
		{
			const unsigned char *entry = db + off;
			unsigned inode_entry = entry[0] | (entry[1] << 8) | (entry[2] << 16) | (entry[3] << 24);
			unsigned rec_len = entry[4] | (entry[5] << 8);
			unsigned name_len = entry[6];

			if (rec_len == 0)
				break;

			if (inode_entry != 0 && name_len > 0 && name_len < rec_len)
			{
				std::string name((const char *)(entry + 8), name_len);

				/* Leer inode de la entrada para obtener tamaño/tiempos/modo */
				if (inode_entry == 0 || inode_entry > (unsigned)DatosFS.DatosEspecificos.EXT.NumeroDeINodes)
				{
					off += rec_len;
					continue;
				}

				unsigned grupo_e = (inode_entry - 1) / inodes_por_grupo;
				if (grupo_e >= nro_grupos)
				{
					off += rec_len;
					continue;
				}

				unsigned long long tabla_inodos_e = DatosFS.DatosEspecificos.EXT.DatosGrupo[grupo_e].ClusterTablaINodes;
				unsigned index_in_group_e = (inode_entry - 1) % inodes_por_grupo;
				unsigned offset_in_table_e = index_in_group_e * bytes_por_inode;
				unsigned block_offset_e = offset_in_table_e / DatosFS.BytesPorCluster;
				unsigned offset_in_block_e = offset_in_table_e % DatosFS.BytesPorCluster;
				unsigned block_e = (unsigned)(tabla_inodos_e + block_offset_e);

				const unsigned char *pblock_e = PunteroASector((__u64)block_e * sectores_por_cluster);
				if (!pblock_e)
				{
					off += rec_len;
					continue;
				}

				TINodeEXT inode_e;
				memset(&inode_e, 0, sizeof(inode_e));
				if (offset_in_block_e + bytes_por_inode <= (unsigned)DatosFS.BytesPorCluster)
				{
					memcpy(&inode_e, pblock_e + offset_in_block_e, (bytes_por_inode < sizeof(TINodeEXT)) ? bytes_por_inode : sizeof(TINodeEXT));
				}
				else
				{
					unsigned first_chunk = (unsigned)DatosFS.BytesPorCluster - offset_in_block_e;
					unsigned to_copy = (bytes_por_inode < sizeof(TINodeEXT)) ? bytes_por_inode : sizeof(TINodeEXT);
					unsigned copied = 0;
					if (first_chunk > 0)
					{
						unsigned c = (first_chunk < to_copy) ? first_chunk : to_copy;
						memcpy(&((unsigned char*)&inode_e)[0], pblock_e + offset_in_block_e, c);
						copied += c;
					}
					if (copied < to_copy)
					{
						const unsigned char *pblock2 = PunteroASector((__u64)(block_e + 1) * sectores_por_cluster);
						if (pblock2)
							memcpy(&((unsigned char*)&inode_e)[copied], pblock2, to_copy - copied);
					}
				}

				/* Construir entrada */
				TEntradaDirectorio e;
				memset(&e, 0, sizeof(e));
				e.Nombre = name;
				unsigned long long size = (unsigned long long)inode_e.i_size_lo;
				size |= ((unsigned long long)inode_e.i_size_high) << 32;
				e.Bytes = size;
				e.FechaCreacion = (time_t)inode_e.i_crtime;
				e.FechaUltimoAcceso = (time_t)inode_e.i_atime;
				e.FechaUltimaModificacion = (time_t)inode_e.i_mtime;
				e.Flags = 0;
				if (S_ISDIR(inode_e.i_mode))
					e.Flags |= fedDIRECTORIO;
				e.DatosEspecificos.EXT.INode = inode_entry;

				Entradas.push_back(e);
			}

			off += rec_len;
		}
	}

	return CODERROR_NINGUNO;
}

/****************************************************************************************************************************************
 *																	*
 *						     TDriverEXT :: LeerArchivo								*
 *																	*
 * OBJETIVO: Esta función levanta de la imágen un archivo dada su ruta.									*
 *																	*
 * ENTRADA: Path: Ruta al archivo a levantar.												*
 *																	*
 * SALIDA: En el nombre de la función CODERROR_NINGUNO si no hubo errores, caso contrario el código de error.				*
 *	   Data: Buffer alocado con malloc() con los datos del archivo.									*
 *	   DataLen: Tamaño en bytes del buffer devuelto.										*
 *																	*						*
 ****************************************************************************************************************************************/
int TDriverEXT::LeerArchivo(const char *Path, unsigned char *&Data, unsigned &DataLen)
{
/* Salir */
return(CODERROR_NO_IMPLEMENTADO);
}