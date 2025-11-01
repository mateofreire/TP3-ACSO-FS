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
    unsigned s_r_blocks_count    = RD32(0x08);
	unsigned s_log_block_size    = RD32(0x18);
	unsigned s_blocks_per_group  = RD32(0x20);
	unsigned s_inodes_per_group  = RD32(0x28);
	unsigned s_inode_size        = RD16(0x58);
	unsigned s_feat_compat       = RD32(0x5C);
	unsigned s_feat_incompat     = RD32(0x60);
	unsigned s_feat_ro_compat    = RD32(0x64);

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

        /* ---------- Levantar descriptores de grupo (GDT) ---------- */
    DatosFS.DatosEspecificos.EXT.DatosGrupo.clear();
    DatosFS.DatosEspecificos.EXT.DatosGrupo.resize(DatosFS.DatosEspecificos.EXT.NroGrupos);

    unsigned tamDesc = sizeof(TEntradaDescGrupoEXT23);
    unsigned tamBloque = DatosFS.BytesPorCluster;
    unsigned descPorBloque = tamBloque / tamDesc;

    // La tabla de descriptores arranca justo después del superbloque:
    // En EXT2 con bloque de 1024B: superbloque = bloque 1 → GDT = bloque 2
    // (Si el bloque fuera >1024B también arranca en 2)
    unsigned bloqueInicioGDT = 2;
    unsigned sectoresPorBloque = DatosFS.BytesPorCluster / DatosFS.BytesPorSector;

    for (int g = 0; g < DatosFS.DatosEspecificos.EXT.NroGrupos; g++) {
        unsigned bloque = bloqueInicioGDT + (g / descPorBloque);
        unsigned offset = (g % descPorBloque) * tamDesc;

        const unsigned char *ptr = PunteroASector((__u64)bloque * sectoresPorBloque);
        if (!ptr) continue;

        const TEntradaDescGrupoEXT23 *desc =
            reinterpret_cast<const TEntradaDescGrupoEXT23 *>(ptr + offset);

        DatosFS.DatosEspecificos.EXT.DatosGrupo[g].ClusterBitmapBloques = (__u64)desc->block_bitmap;
        DatosFS.DatosEspecificos.EXT.DatosGrupo[g].ClusterBitmapINodes  = (__u64)desc->inode_bitmap;
        DatosFS.DatosEspecificos.EXT.DatosGrupo[g].ClusterTablaINodes   = (__u64)desc->inode_table;
        // cantidad de bloques que ocupa la tabla de i-nodes en este grupo
        unsigned blocks_inode_table =
            (unsigned)((DatosFS.DatosEspecificos.EXT.INodesPorGrupo * DatosFS.DatosEspecificos.EXT.BytesPorINode
                    + DatosFS.BytesPorCluster - 1) / DatosFS.BytesPorCluster);

        // primer bloque de datos del grupo (lo que la cátedra llama “Tabla Bloques”)
        DatosFS.DatosEspecificos.EXT.DatosGrupo[g].ClusterTablaBloques =
            (__u64)(desc->inode_table + blocks_inode_table);
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
/* Salir */
return(CODERROR_NO_IMPLEMENTADO);
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