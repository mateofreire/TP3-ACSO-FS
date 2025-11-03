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
	/* EXT2 usa 512 B/sector */
	DatosFS.BytesPorSector = 512;

	/* El superbloque está a 1024 bytes desde el inicio (sector lógico 2) */
	const unsigned char *sb = PunteroASector(2);
	if (!sb)
		return CODERROR_SUPERBLOQUE_INVALIDO;

	/* Helpers de lectura (valores little-endian en disco) */
    #define RD16(off)  ((unsigned short)(sb[off] | (sb[(off)+1] << 8)))
    #define RD32(off)  ((unsigned int)(sb[off] | (sb[(off)+1] << 8) | (sb[(off)+2] << 16) | (sb[(off)+3] << 24)))

	/* Validar firma del superbloque (offset 0x38 dentro del superbloque) */
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

	/* BlockSize = 1024 << s_log_block_size */
	DatosFS.TipoFilesystem               = tfsEXT2;
	DatosFS.BytesPorCluster              = 1024u << s_log_block_size;
	DatosFS.NumeroDeClusters             = s_blocks_count_lo;

	/* Campos específicos */
	DatosFS.DatosEspecificos.EXT.CaracteristicasCompatibles   = (int)s_feat_compat;
	DatosFS.DatosEspecificos.EXT.CaracteristicasIncompatibles = (int)s_feat_incompat;
	DatosFS.DatosEspecificos.EXT.CaracteristicasSoloLectura   = (int)s_feat_ro_compat;
	DatosFS.DatosEspecificos.EXT.NumeroDeINodes               = (int)s_inodes_count;
    DatosFS.DatosEspecificos.EXT.ClustersReservadosGDT        = (int)s_r_blocks_count;
	DatosFS.DatosEspecificos.EXT.ClustersPorGrupo             = (int)s_blocks_per_group;
	DatosFS.DatosEspecificos.EXT.INodesPorGrupo               = (int)s_inodes_per_group;
	DatosFS.DatosEspecificos.EXT.BytesPorINode                = (int)s_inode_size;
	DatosFS.DatosEspecificos.EXT.PeriodoAgrupadoFlex = 0;

	/* Derivados: número de grupos */
	if (DatosFS.DatosEspecificos.EXT.ClustersPorGrupo == 0)
		return CODERROR_SUPERBLOQUE_INVALIDO;

	DatosFS.DatosEspecificos.EXT.NroGrupos = DatosFS.NumeroDeClusters / DatosFS.DatosEspecificos.EXT.ClustersPorGrupo;

	/* Leer la Tabla de Descriptores de Grupo (GDT) con TEntradaDescGrupoEXT23 y completar DatosGrupo */
	{
		unsigned desc_size = sizeof(TEntradaDescGrupoEXT23);
		unsigned sectors_per_cluster = DatosFS.BytesPorCluster / DatosFS.BytesPorSector;

		/* GDT comienza en el bloque 2 si el tamaño de bloque es 1024, sino en el bloque 1 */
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

			/* Lectura little endian desde p */
			#define RD32P(o) ((unsigned int)(p[(o)] | (p[(o)+1] << 8) | (p[(o)+2] << 16) | (p[(o)+3] << 24)))

			unsigned block_bitmap = RD32P(0);
			unsigned inode_bitmap = RD32P(4);
			unsigned inode_table  = RD32P(8);

			DatosFS.DatosEspecificos.EXT.DatosGrupo[i].ClusterBitmapINodes = (unsigned long long)inode_bitmap;
			DatosFS.DatosEspecificos.EXT.DatosGrupo[i].ClusterBitmapBloques = (unsigned long long)block_bitmap;
			DatosFS.DatosEspecificos.EXT.DatosGrupo[i].ClusterTablaINodes = (unsigned long long)inode_table;

			/* Calcular el primer bloque de datos del grupo, que está justo después de la tabla de inodos */
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
	Entradas.clear();

	if (!Path)
		return CODERROR_PARAMETROS_INVALIDOS;

	if (Path[0] != '/')
		return CODERROR_RUTA_NO_ABSOLUTA;

	if (DatosFS.TipoFilesystem != tfsEXT2)
		return CODERROR_FILESYSTEM_DESCONOCIDO;

	unsigned sectores_por_cluster = DatosFS.BytesPorCluster / DatosFS.BytesPorSector;

	unsigned inodes_por_grupo = (unsigned)DatosFS.DatosEspecificos.EXT.INodesPorGrupo;
	unsigned bytes_por_inode = (unsigned)DatosFS.DatosEspecificos.EXT.BytesPorINode;
	unsigned nro_grupos = (unsigned)DatosFS.DatosEspecificos.EXT.NroGrupos;

	/* Resolver la ruta componente a componente, empezando en la raiz (inode 2) */
	unsigned current_inode = EXT_ROOT_INO; /* 2 */
	std::string ruta(Path);

	if (ruta != "/")
	{
		size_t pos = 1; /* Saltar la primer / */
		while (pos < ruta.size())
		{
			size_t next = ruta.find('/', pos);
			std::string componente = (next==std::string::npos) ? ruta.substr(pos) : ruta.substr(pos, next-pos);
			if (componente.empty())
			{
				pos = (next==std::string::npos)? ruta.size() : next+1;
				continue;
			}

			/* Leer inode del directorio actual */
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

			/* Copiar el inode */
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

	/* Ahora current_inode es el inodo del directorio a listar, lo leemos y listamos sus entradas */
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

	/* Recorremos los bloques directos y añadimos cada entrada a Entradas */
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
				/* la fecha de creacion esta mal en el diff pero no entendemos porque si todo el resto de las fechas estan bien (como que no es del modo de lectura de little endian porque es el mismo en todas las entradas, es como que ni aparece)*/
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
	Data = NULL;
	DataLen = 0;

	if (!Path)
		return CODERROR_PARAMETROS_INVALIDOS;

	if (Path[0] != '/')
		return CODERROR_RUTA_NO_ABSOLUTA;

	if (DatosFS.TipoFilesystem != tfsEXT2)
		return CODERROR_FILESYSTEM_DESCONOCIDO;

	unsigned sectores_por_cluster = DatosFS.BytesPorCluster / DatosFS.BytesPorSector;

	unsigned inodes_por_grupo = (unsigned)DatosFS.DatosEspecificos.EXT.INodesPorGrupo;
	unsigned bytes_por_inode = (unsigned)DatosFS.DatosEspecificos.EXT.BytesPorINode;
	unsigned nro_grupos = (unsigned)DatosFS.DatosEspecificos.EXT.NroGrupos;

	/* Resolver la ruta igual que en ListarDirectorio */
	unsigned current_inode = EXT_ROOT_INO; /* inodo 2 */
	std::string ruta(Path);

	if (ruta != "/")
	{
		size_t pos = 1;
		while (pos < ruta.size())
		{
			size_t next = ruta.find('/', pos);
			std::string componente = (next==std::string::npos) ? ruta.substr(pos) : ruta.substr(pos, next-pos);
			if (componente.empty())
			{
				pos = (next==std::string::npos)? ruta.size() : next+1;
				continue;
			}

			if (current_inode == 0 || current_inode > (unsigned)DatosFS.DatosEspecificos.EXT.NumeroDeINodes)
				return CODERROR_ARCHIVO_INEXISTENTE;

			unsigned grupo = (current_inode - 1) / inodes_por_grupo;
			if (grupo >= nro_grupos)
				return CODERROR_ARCHIVO_INEXISTENTE;

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

			if (offset_in_block + bytes_por_inode <= (unsigned)DatosFS.BytesPorCluster)
			{
				memcpy(&inode_dir, pblock + offset_in_block, (bytes_por_inode < sizeof(TINodeEXT)) ? bytes_por_inode : sizeof(TINodeEXT));
			}
			else
			{
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
					const unsigned char *pblock2 = PunteroASector((__u64)(block + 1) * sectores_por_cluster);
					if (!pblock2)
						return CODERROR_LECTURA_DISCO;
					memcpy(&((unsigned char*)&inode_dir)[copied], pblock2, to_copy - copied);
				}
			}

			/* Verificar que sea directorio para poder buscar */
			if (!S_ISDIR(inode_dir.i_mode))
				return CODERROR_ARCHIVO_INEXISTENTE;

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
				return CODERROR_ARCHIVO_INEXISTENTE;

			pos = (next==std::string::npos)? ruta.size() : next+1;
		}
	}

	/* Ahora current_inode es el inode del archivo a leer*/
	if (current_inode == 0 || current_inode > (unsigned)DatosFS.DatosEspecificos.EXT.NumeroDeINodes)
		return CODERROR_ARCHIVO_INEXISTENTE;

	unsigned grupo_file = (current_inode - 1) / inodes_por_grupo;
	if (grupo_file >= nro_grupos)
		return CODERROR_ARCHIVO_INEXISTENTE;

	unsigned long long tabla_inodos_file = DatosFS.DatosEspecificos.EXT.DatosGrupo[grupo_file].ClusterTablaINodes;
	unsigned index_in_group_file = (current_inode - 1) % inodes_por_grupo;
	unsigned offset_in_table_file = index_in_group_file * bytes_por_inode;
	unsigned block_offset_file = offset_in_table_file / DatosFS.BytesPorCluster;
	unsigned offset_in_block_file = offset_in_table_file % DatosFS.BytesPorCluster;
	unsigned block_file = (unsigned)(tabla_inodos_file + block_offset_file);

	const unsigned char *pblock_file = PunteroASector((__u64)block_file * sectores_por_cluster);
	if (!pblock_file)
		return CODERROR_LECTURA_DISCO;

	TINodeEXT inode_file;
	memset(&inode_file, 0, sizeof(inode_file));
	if (offset_in_block_file + bytes_por_inode <= (unsigned)DatosFS.BytesPorCluster)
	{
		memcpy(&inode_file, pblock_file + offset_in_block_file, (bytes_por_inode < sizeof(TINodeEXT)) ? bytes_por_inode : sizeof(TINodeEXT));
	}
	else
	{
		unsigned first_chunk = (unsigned)DatosFS.BytesPorCluster - offset_in_block_file;
		unsigned to_copy = (bytes_por_inode < sizeof(TINodeEXT)) ? bytes_por_inode : sizeof(TINodeEXT);
		unsigned copied = 0;
		if (first_chunk > 0)
		{
			unsigned c = (first_chunk < to_copy) ? first_chunk : to_copy;
			memcpy(&((unsigned char*)&inode_file)[0], pblock_file + offset_in_block_file, c);
			copied += c;
		}
		if (copied < to_copy)
		{
			const unsigned char *pblock2 = PunteroASector((__u64)(block_file + 1) * sectores_por_cluster);
			if (!pblock2)
				return CODERROR_LECTURA_DISCO;
			memcpy(&((unsigned char*)&inode_file)[copied], pblock2, to_copy - copied);
		}
	}

	/* No podemos leer directorios como archivos */
	if (S_ISDIR(inode_file.i_mode))
		return CODERROR_ARCHIVO_INEXISTENTE;

	unsigned long long size = (unsigned long long)inode_file.i_size_lo;
	size |= ((unsigned long long)inode_file.i_size_high) << 32;

	if (size == 0)
	{
		Data = NULL;
		DataLen = 0;
		return CODERROR_NINGUNO;
	}

	/* Truncar a 32 bits */
	if (size > (unsigned long long)UINT_MAX)
		return CODERROR_ARCHIVO_INVALIDO;

	DataLen = (unsigned)size;
	Data = (unsigned char*)malloc(DataLen);
	if (!Data)
		return CODERROR_FALTA_MEMORIA;

	unsigned cluster_size = (unsigned)DatosFS.BytesPorCluster;
	unsigned blocks_needed = (DataLen + cluster_size - 1) / cluster_size;
	unsigned copied_total = 0;

	/* Helper macro para leer uint32 little-endian desde un puntero p en offset o índice */
	#define RD32_FROM_PTR(p, off) ((unsigned)((p)[(off)] | ((p)[(off)+1] << 8) | ((p)[(off)+2] << 16) | ((p)[(off)+3] << 24)))

	unsigned per_block_ptrs = cluster_size / 4;

	for (unsigned lb = 0; lb < blocks_needed; lb++)
	{
		unsigned phys_block = 0;

		if (lb < 12)
		{
			phys_block = (unsigned)inode_file.i_block[lb];
		}
		else if (lb < 12 + per_block_ptrs)
		{
			/* Indirecto simple */
			unsigned idx = lb - 12;
			unsigned indirect_block = (unsigned)inode_file.i_block[12];
			if (indirect_block == 0)
			{
				phys_block = 0;
			}
			else
			{
				const unsigned char *ind = PunteroASector((__u64)indirect_block * sectores_por_cluster);
				if (!ind)
				{
					free(Data);
					Data = NULL;
					DataLen = 0;
					return CODERROR_LECTURA_DISCO;
				}
				phys_block = RD32_FROM_PTR(ind, idx*4);
			}
		}
		else if (lb < 12 + per_block_ptrs + per_block_ptrs * per_block_ptrs)
		{
			/* Indirecto doble */
			unsigned rem = lb - (12 + per_block_ptrs);
			unsigned idx1 = rem / per_block_ptrs;
			unsigned idx2 = rem % per_block_ptrs;
			unsigned dbl_block = (unsigned)inode_file.i_block[13];
			if (dbl_block == 0)
			{
				phys_block = 0;
			}
			else
			{
				const unsigned char *dbl = PunteroASector((__u64)dbl_block * sectores_por_cluster);
				if (!dbl)
				{
					free(Data);
					Data = NULL;
					DataLen = 0;
					return CODERROR_LECTURA_DISCO;
				}
				unsigned first_level = RD32_FROM_PTR(dbl, idx1*4);
				if (first_level == 0)
				{
					phys_block = 0;
				}
				else
				{
					const unsigned char *ind2 = PunteroASector((__u64)first_level * sectores_por_cluster);
					if (!ind2)
					{
						free(Data);
						Data = NULL;
						DataLen = 0;
						return CODERROR_LECTURA_DISCO;
					}
					phys_block = RD32_FROM_PTR(ind2, idx2*4);
				}
			}
		}
		else
		{
			/* Triple indirecto no lo implementamos */
			phys_block = 0;
		}

		/* Copiar */
		unsigned to_copy = min(cluster_size, DataLen - copied_total);
		if (phys_block == 0)
		{
			memset(Data + copied_total, 0, to_copy);
		}
		else
		{
			const unsigned char *pdat = PunteroASector((__u64)phys_block * sectores_por_cluster);
			if (!pdat)
			{
				free(Data);
				Data = NULL;
				DataLen = 0;
				return CODERROR_LECTURA_DISCO;
			}
			memcpy(Data + copied_total, pdat, to_copy);
		}

		copied_total += to_copy;
	}

	#undef RD32_FROM_PTR

	return CODERROR_NINGUNO;
}