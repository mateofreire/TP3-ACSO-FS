#ifndef	__DRIVER_BASE__H__
#define	__DRIVER_BASE__H__

/************************
 *			*
 *        Tipos		*
 *			*
 ************************/
/* Cadenas de caracteres */
typedef	std::basic_string<char>	TString;

/************************
 *			*
 *        Tipos		*
 *			*
 ************************/
/* Tipos de enteros de distintos tamaños */
typedef	char			__le8;
typedef	unsigned char		__u8;
typedef	short			__le16;
typedef	unsigned short		__u16;
typedef	int			__le32;
typedef	unsigned int		__u32;
typedef	long long		__le64;
typedef	unsigned long long	__u64;

/* Clase que utiliza los drivers derivados de esta clase */
class TAnalizadorFS;


/************************
 *			*
 *     Estructuras	*
 *			*
 ************************/
/* Tipos de filesystem soportados */
typedef	enum
    {
	tfsDESCONOCIDO			= 0,
	tfsFAT12			= 1,
	tfsFAT16			= 2,
	tfsFAT32			= 3,
	tfsEXT2				= 4,
	tfsEXT3				= 5,
	tfsEXT4				= 6,
	tfsNTFS				= 7
    }	TipoFilsystem;

/* Datos de un FS en formato FAT */
typedef	struct
    {
	/* Datos del superbloque */
	int				SectoresPorCluster;
	int				SectoresReservados;
	int				CopiasFAT;
	int				EntradasRootDir;
	int				SectoresPorFAT;
	int				TotalSectores;
	int				SectoresOcultos;

	/* Datos calculados */
	int				ClustersRootDir;
	int				PrimerClusterRootDir;
	
    }	TDatosFSFAT;

/* Datos de un grupo en un FS en formato EXT */
typedef	struct
    {
	__u64				ClusterBitmapINodes;
	__u64				ClusterBitmapBloques;
	__u64				ClusterTablaINodes;
	__u64				ClusterTablaBloques;
    }	TDatosGrupoFSEXT;

/* Datos de un FS en formato EXT */
typedef struct
    {
	/* Datos del superbloque */
	int				CaracteristicasCompatibles;
	int				CaracteristicasIncompatibles;
	int				CaracteristicasSoloLectura;
	int				NumeroDeINodes;
	int				ClustersPorGrupo;
	int				INodesPorGrupo;
	int				ClustersReservadosGDT;
	int				BytesPorINode;
	int				PeriodoAgrupadoFlex;

	/* Datos calculados */
	int				NroGrupos;
	std::vector<TDatosGrupoFSEXT>	DatosGrupo;
	
    }	TDatosFSEXT;

/* Datos de un FS en formato NTFS */
typedef	struct
    {
	__u64				TotalSectores;
	int				SectoresPorCluster;
	__u64				ClusterMFT;
	__u64				ClusterMFTMirror;
	__le32				BytesPorFileRecordSegment;
	__le32				BytesPorIndexBuffer;
	int				OffsetParticionEnSectores;
    }	TDatosFSNTFS;

/* Datos de todo filesystem */
typedef	struct
    {
	/* Datos del superbloque */
	TipoFilsystem			TipoFilesystem;
	int				BytesPorSector;
	int				NumeroDeClusters;
	int				BytesPorCluster;

	class
	{
	public:	    
		TDatosFSFAT		FAT;
		TDatosFSEXT		EXT;
		TDatosFSNTFS		NTFS;
	}	DatosEspecificos;
    }	TDatosFS;


/* Flags de una entrada de directorio */
#define	fedSOLO_LECTURA			0x00000001
#define	fedOCULTO			0x00000002
#define	fedSISTEMA			0x00000004
#define	fedETIQUETA_VOLUMEN		0x00000008
#define	fedDIRECTORIO			0x00000010
#define	fedARCHIVAR			0x00000020
#define	fedACCESO_DIRECTO		0x00000040
#define	fedCOMPRIMIDO			0x00000080
#define	fedENCRIPTADO			0x00000100
#define	fedDISPERSO			0x00000200


/* Propiedades de elementos de una entrada de directorio propios de formato FAT */
typedef	struct
    {
	unsigned			PrimerCluster;
    }	TEntradaFSFAT;
	
/* Propiedades de elementos de una entrada de directorio propios de formato EXT */
typedef	struct
    {
	unsigned			INode;
    }	TEntradaFSEXT;
	
/* Propiedades de elementos de una entrada de directorio propios de formato NTFS */
typedef	struct
    {
	__u64				IndiceMFT;
	unsigned short			NroSecuencia;
    }	TEntradaFSNTFS;
	
/* Estructura para describir los posibles elementos dentro de un directorio de cualquier filesystem */
typedef struct
    {
	unsigned			Flags;
	TString				Nombre;
	__u64				Bytes;
	time_t				FechaCreacion;
	time_t				FechaUltimoAcceso;
	time_t				FechaUltimaModificacion;

	union
	    {
		TEntradaFSFAT		FAT;
		TEntradaFSEXT		EXT;
		TEntradaFSNTFS		NTFS;
	    }				DatosEspecificos;
    }	TEntradaDirectorio;


/********************************
 *				*
 *      Clase TDriverBase	*
 *				*
 ********************************/
class TDriverBase
{
public:
					TDriverBase(const unsigned char *DiskData, unsigned LongitudDiskData);
	virtual				~TDriverBase();
	
protected:
	TDatosFS			DatosFS;

	virtual const unsigned char	*PunteroASector(__u64 NroSector);
	
	/* Funciones a implementar por el alumno */
	virtual int			LevantarDatosSuperbloque() = 0;
	virtual int 			ListarDirectorio(const char *Path, std::vector<TEntradaDirectorio> &Entradas) = 0;
	virtual int 			LeerArchivo(const char *Path, unsigned char *&Data, unsigned &DataLen) = 0;

private:
	unsigned			LongitudDiskData;
	const unsigned char		*DiskData;

	virtual int			MostrarDatosSuperbloque(void);
	virtual int			MostrarDatosDirectorio(std::vector<TEntradaDirectorio> &Entradas);
	virtual void 			PrintBuffer(const unsigned char *Buffer, unsigned BufferLen, unsigned BytesPorLinea);

	
	friend				TAnalizadorFS;
};

#endif
