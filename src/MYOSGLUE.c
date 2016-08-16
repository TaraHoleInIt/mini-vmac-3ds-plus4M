/*
	MYOSGLUE.c

	Copyright (C) 2012 Paul C. Pratt

	You can redistribute this file and/or modify it under the terms
	of version 2 of the GNU General Public License as published by
	the Free Software Foundation.  You should have received a copy
	of the license along with this file; see the file COPYING.

	This file is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	license for more details.
*/

/*
 * Operating system glue for the Nintendo 3DS
 * 2016 Tara Keeling
*/

#include "CNFGRAPI.h"
#include "SYSDEPNS.h"
#include "ENDIANAC.h"

#include "MYOSGLUE.h"

#include "STRCONST.h"

/*
 * PICA200 Vertex shader
 */
#include "vshader_shbin.h"

// Used to transfer the final rendered display to the framebuffer
#define DISPLAY_TRANSFER_FLAGS \
(GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

#define TEXTURE_TRANSFER_FLAGS \
(GX_TRANSFER_FLIP_VERT(1) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) | \
GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) | \
GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

#define MyScreenWidth 400
#define MyScreenHeight 240

static int Video_SetupRenderTarget( void );
static int Video_SetupShader( void );
static int Video_CreateTexture( void );

/*
 * Lookup table used for indexed->RGB565
 */
static u16 FBConvTable[ 256 ][ 8 ];

C3D_RenderTarget* MainRenderTarget = NULL;
C3D_RenderTarget* SubRenderTarget = NULL;

static DVLB_s* Shader = NULL;
static shaderProgram_s Program;

int LocProjectionUniforms = 0;

C3D_Tex FBTexture;

C3D_Mtx ProjectionMain;
C3D_Mtx ProjectionSub;

static void* TempTextureBuffer = NULL;

typedef enum {
    INFB_FORMAT_1BPP = 1,
    INFB_FORMAT_4BPP,
    INFB_FORMAT_8BPP
} INFB_FORMAT;

/*
 * Converts a 1bpp packed image and outputs it in RGB565 format.
 */
void Convert1BPP( u8* Src, u32* Dest, int Size ) {
    const u32 HNibble2Pixels[ 4 ] = {
        0xFFFFFFFF, // 00
        0x0000FFFF, // 01
        0xFFFF0000, // 10
        0x00000000  // 11
    };
    u8 In = 0;
    
    while ( Size-= 8 ) {
        In = *Src++;
        
        *Dest++ = HNibble2Pixels[ ( In >> 6 ) & 0x03 ];
        *Dest++ = HNibble2Pixels[ ( In >> 4 ) & 0x03 ];
        *Dest++ = HNibble2Pixels[ ( In >> 2 ) & 0x03 ];
        *Dest++ = HNibble2Pixels[ In & 0x03 ];
    }
}

/*
 * Converts an 8bpp paletted image and outputs it in RGB565 format.
 */
static void Convert8BPP( u8* Src, u16* Dest, int Size ) {
    while ( Size-- ) {
        *Dest++ = FBConvTable[ *Src++ ][ 0 ];
    }
}

/*
 * Converts an 4bpp packed image and outputs it in RGB565 format.
 */
static void Convert4BPP( u8* Src, u16* Dest, int Size ) {
    while ( Size-= 2 ) {
        memcpy( Dest, &FBConvTable[ *Src ][ 0 ], 2 * 2 );
        
        Dest+= 2;
        Src++;
    }
}

/*
 * Performs an indexed to RGB565 framebuffer conversion
 */
static void ConvertFBTo565( u8* Src, u16* Dest, int Size, INFB_FORMAT Format ) {
    switch ( Format ) {
        case INFB_FORMAT_1BPP: {
            Convert1BPP( Src, Dest, Size );
            break;
        }
        case INFB_FORMAT_4BPP: {
            Convert4BPP( Src, Dest, Size );
            break;
        }
        case INFB_FORMAT_8BPP: {
            Convert8BPP( Src, Dest, Size );
            break;
        }
        default: break;
    };
}

/*
 * Sets up the 8bpp->16bpp paletted to RGB565 conversion table.
 */
void MakeTable8BPP( u16* Reds, u16* Greens, u16* Blues, int Size ) {
    int i = 0;
    
    for ( i = 0; i < Size; i++ ) {
        FBConvTable[ i ][ 0 ] = RGB8_to_565( Reds[ i ] >> 8, Greens[ i ] >> 8, Blues[ i ] >> 8 );
    }
}

/*
 * Sets up the 4bpp->16bpp paletted to RGB565 conversion table.
 */
void MakeTable4BPP( u16* Reds, u16* Greens, u16* Blues, int Size ) {
    u8 l;
    u8 h;
    int i = 0;
    
    for ( i = 0; i < 256; i++ ) {
        l = i & 0x0F;
        h = i >> 4;
        
        FBConvTable[ i ][ 1 ] = RGB8_to_565( Reds[ l ] >> 8, Greens[ l ] >> 8, Blues[ l ] >> 8 );
        FBConvTable[ i ][ 0 ] = RGB8_to_565( Reds[ h ] >> 8, Greens[ h ] >> 8, Blues[ h ] >> 8 );
    }
}

void Video_UpdateTexture( u8* Src, int Size, INFB_FORMAT Format ) {
    u16* TempBuffer = ( u16* ) TempTextureBuffer;
    
    if ( TempTextureBuffer ) {
        ConvertFBTo565( Src, ( u16* ) TempBuffer, Size, Format );
        GSPGPU_FlushDataCache( TempBuffer, 512 * 512 * 2 );
        
        C3D_SafeDisplayTransfer( ( u32* ) TempBuffer, GX_BUFFER_DIM( 512, 512 ), ( u32* ) FBTexture.data, GX_BUFFER_DIM( 512, 512 ), TEXTURE_TRANSFER_FLAGS );
        //gspWaitForPPF( );
    }
}

void Video_UpdateTexture2( u8* Src, int Size, INFB_FORMAT Format, int StartY, int EndY ) {
    u16* TempBuffer = ( u16* ) TempTextureBuffer;
    
    switch ( Format ) {
        case INFB_FORMAT_1BPP: {
            Src+= ( StartY * ( vMacScreenWidth / 8 ) );
            TempBuffer+= ( StartY * 512 );
            
            Size = ( EndY - StartY ) * vMacScreenWidth;
            break;
        }
        case INFB_FORMAT_4BPP: {
            Src+= ( StartY * ( vMacScreenWidth / 2 ) );
            TempBuffer+= ( StartY * 512 );
            
            Size = ( ( EndY - StartY ) * vMacScreenWidth );
            break;
        }
        case INFB_FORMAT_8BPP: {
            Src+= ( StartY * vMacScreenWidth );
            TempBuffer+= ( StartY * 512 );
            
            Size = ( EndY - StartY ) * vMacScreenWidth;
            break;
        }
        default: break;
    };
    
    if ( TempTextureBuffer ) {
        ConvertFBTo565( Src, ( u16* ) TempBuffer, Size, Format );
        GSPGPU_FlushDataCache( TempTextureBuffer, 512 * 512 * 2 );
        
        C3D_SafeDisplayTransfer( ( u32* ) TempTextureBuffer, GX_BUFFER_DIM( 512, 512 ), ( u32* ) FBTexture.data, GX_BUFFER_DIM( 512, 512 ), TEXTURE_TRANSFER_FLAGS );
    }
}

void FB_Draw( C3D_RenderTarget* Target, float X, float Y, float ScaleX, float ScaleY ) {
    int Width = 512 * ScaleX;
    int Height = 512 * ScaleY;
    float u = 1.0;
    float v = 1.0;
    
    if ( Target ) {
        C3D_FrameDrawOn( Target );
        C3D_FVUnifMtx4x4( GPU_VERTEX_SHADER, LocProjectionUniforms, &ProjectionMain );
        C3D_TexBind( 0, &FBTexture );
        
        C3D_ImmDrawBegin( GPU_TRIANGLES );
        // 1st triangle
        C3D_ImmSendAttrib( X, Y, 1.0, 0.0 );
        C3D_ImmSendAttrib( 0.0, 0.0, 0.0, 0.0 );
        
        C3D_ImmSendAttrib( X + Width, Y + Height, 1.0, 0.0 );
        C3D_ImmSendAttrib( u, v, 0.0, 0.0 );
        
        C3D_ImmSendAttrib( X + Width, Y, 1.0, 0.0 );
        C3D_ImmSendAttrib( u, 0.0, 0.0, 0.0 );
        
        // 2nd triangle
        C3D_ImmSendAttrib( X, Y, 1.0, 0.0 );
        C3D_ImmSendAttrib( 0.0, 0.0, 0.0, 0.0 );
        
        C3D_ImmSendAttrib( X, Y + Height, 1.0, 0.0 );
        C3D_ImmSendAttrib( 0.0, v, 0.0, 0.0 );
        
        C3D_ImmSendAttrib( X + Width, Y + Height, 1.0, 0.0 );
        C3D_ImmSendAttrib( u, v, 0.0, 0.0 );
        C3D_ImmDrawEnd( );
    }
}

static int Video_SetupRenderTarget( void ) {
    MainRenderTarget = C3D_RenderTargetCreate( 240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8 );
    //SubRenderTarget = C3D_RenderTargetCreate( 240, 320, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8 );
    
    if ( MainRenderTarget /*&& SubRenderTarget*/ ) {
        C3D_RenderTargetSetClear( MainRenderTarget, C3D_CLEAR_ALL, 0xFF, 0 );
        C3D_RenderTargetSetOutput( MainRenderTarget, GFX_TOP, GFX_LEFT, DISPLAY_TRANSFER_FLAGS );
        
        //C3D_RenderTargetSetClear( SubRenderTarget, C3D_CLEAR_ALL, 0xFFFF, 0 );
        //C3D_RenderTargetSetOutput( SubRenderTarget, GFX_BOTTOM, GFX_LEFT, DISPLAY_TRANSFER_FLAGS );
        
        return 1;
    }
    
    return 0;
}

static int Video_SetupShader( void ) {
    C3D_AttrInfo* Info = NULL;
    
    Shader = DVLB_ParseFile( ( u32* ) vshader_shbin, ( u32 ) vshader_shbin_size );
    
    if ( Shader ) {
        shaderProgramInit( &Program );
        shaderProgramSetVsh( &Program, &Shader->DVLE[ 0 ] );
        
        C3D_BindProgram( &Program );
        
        LocProjectionUniforms = shaderInstanceGetUniformLocation( Program.vertexShader, "projection" );
        Info = C3D_GetAttrInfo( );
        
        if ( Info ) {
            AttrInfo_Init( Info );
            AttrInfo_AddLoader( Info, 0, GPU_FLOAT, 3 );
            AttrInfo_AddLoader( Info, 1, GPU_FLOAT, 2 );
            //AttrInfo_AddLoader( Info, 2, GPU_FLOAT, 4 );
            
            //BufInfo_Init( C3D_GetBufInfo( ) );
            return 1;
        }
    }
    
    return 0;
}

static int Video_CreateTexture( void ) {
    C3D_TexEnv* Env = NULL;
    
    C3D_TexInit( &FBTexture, 512, 512, GPU_RGB565 );
    C3D_TexSetFilter( &FBTexture, GPU_NEAREST, GPU_NEAREST );
    
    Env = C3D_GetTexEnv( 0 );
    
    if ( Env ) {
        C3D_TexEnvSrc( Env, C3D_Both, GPU_TEXTURE0, 0, 0 );
        C3D_TexEnvOp( Env, C3D_Both, 0, 0, 0 );
        C3D_TexEnvFunc( Env, C3D_Both, GPU_REPLACE );
    }
    
    return 1;
}

int Video_Init( void ) {
    gfxInitDefault( );
    consoleInit( GFX_BOTTOM, NULL );
    printf( "Hi!\n" );
    
    C3D_Init( C3D_DEFAULT_CMDBUF_SIZE );
    
    Video_SetupRenderTarget( );
    Video_SetupShader( );
    Video_CreateTexture( );
    
    Mtx_OrthoTilt( &ProjectionMain, 0.0, 400.0, 240.0, 0.0, 0.0, 1.0 );
    //Mtx_OrthoTilt( &ProjectionSub, 0.0, 320.0, 240.0, 0.0, 0.0, 1.0 );
    
    C3D_DepthTest( true, GPU_GEQUAL, GPU_WRITE_ALL );
    
    TempTextureBuffer = linearMemAlign( 512 * 512 * 2, 0x80 );
    
    return TempTextureBuffer ? 1 : 0;
}

void Video_Close( void ) {
    if ( TempTextureBuffer )
        linearFree( TempTextureBuffer );
    
    if ( Shader ) {
        shaderProgramFree( &Program );
        DVLB_Free( Shader );
    }
    
    if ( MainRenderTarget ) {
        C3D_RenderTargetDelete( MainRenderTarget );
    }
    
    C3D_TexDelete( &FBTexture );
    C3D_Fini( );
    
    gfxExit( );
}

/* --- some simple utilities --- */

GLOBALPROC MyMoveBytes(anyp srcPtr, anyp destPtr, si5b byteCount)
{
	(void) memcpy((char *)destPtr, (char *)srcPtr, byteCount);
}

/* --- control mode and internationalization --- */

#define NeedCell2PlainAsciiMap 1

#include "INTLCHAR.h"

/* --- sending debugging info to file --- */

#if dbglog_HAVE

#define dbglog_ToStdErr 1

#if ! dbglog_ToStdErr
LOCALVAR FILE *dbglog_File = NULL;
#endif

LOCALFUNC blnr dbglog_open0(void)
{
#if dbglog_ToStdErr
	return trueblnr;
#else
    dbglog_File = open("dbglog.txt", "w");
	return (NULL != dbglog_File);
#endif
}

LOCALPROC dbglog_write0(char *s, uimr L)
{
#if dbglog_ToStdErr
	(void) fwrite(s, 1, L, stderr);
#else
	if (dbglog_File != NULL) {
		(void) fwrite(s, 1, L, dbglog_File);
	}
#endif
}

LOCALPROC dbglog_close0(void)
{
#if ! dbglog_ToStdErr
	if (dbglog_File != NULL) {
		fclose(dbglog_File);
		dbglog_File = NULL;
	}
#endif
}

#endif

/* --- information about the environment --- */

#define WantColorTransValid 0

#include "COMOSGLU.h"

#include "CONTROLM.h"

/* --- parameter buffers --- */

#if IncludePbufs
LOCALVAR void *PbufDat[NumPbufs];
#endif

#if IncludePbufs
LOCALFUNC tMacErr PbufNewFromPtr(void *p, ui5b count, tPbuf *r)
{
	tPbuf i;
	tMacErr err;

	if (! FirstFreePbuf(&i)) {
		free(p);
		err = mnvm_miscErr;
	} else {
		*r = i;
		PbufDat[i] = p;
		PbufNewNotify(i, count);
		err = mnvm_noErr;
	}

	return err;
}
#endif

#if IncludePbufs
GLOBALFUNC tMacErr PbufNew(ui5b count, tPbuf *r)
{
	tMacErr err = mnvm_miscErr;

	void *p = calloc(1, count);
	if (NULL != p) {
		err = PbufNewFromPtr(p, count, r);
	}

	return err;
}
#endif

#if IncludePbufs
GLOBALPROC PbufDispose(tPbuf i)
{
	free(PbufDat[i]);
	PbufDisposeNotify(i);
}
#endif

#if IncludePbufs
LOCALPROC UnInitPbufs(void)
{
	tPbuf i;

	for (i = 0; i < NumPbufs; ++i) {
		if (PbufIsAllocated(i)) {
			PbufDispose(i);
		}
	}
}
#endif

#if IncludePbufs
GLOBALPROC PbufTransfer(ui3p Buffer,
	tPbuf i, ui5r offset, ui5r count, blnr IsWrite)
{
	void *p = ((ui3p)PbufDat[i]) + offset;
	if (IsWrite) {
		(void) memcpy(p, Buffer, count);
	} else {
		(void) memcpy(Buffer, p, count);
	}
}
#endif

/* --- text translation --- */

LOCALPROC NativeStrFromCStr(char *r, char *s)
{
	ui3b ps[ClStrMaxLength];
	int i;
	int L;

	ClStrFromSubstCStr(&L, ps, s);

	for (i = 0; i < L; ++i) {
		r[i] = Cell2PlainAsciiMap[ps[i]];
	}

	r[L] = 0;
}

/* --- drives --- */

#define NotAfileRef NULL

LOCALVAR FILE *Drives[NumDrives]; /* open disk image files */

LOCALPROC InitDrives(void)
{
	/*
		This isn't really needed, Drives[i] and DriveNames[i]
		need not have valid values when not vSonyIsInserted[i].
	*/
	tDrive i;

	for (i = 0; i < NumDrives; ++i) {
		Drives[i] = NotAfileRef;
	}
}

GLOBALFUNC tMacErr vSonyTransfer(blnr IsWrite, ui3p Buffer,
	tDrive Drive_No, ui5r Sony_Start, ui5r Sony_Count,
	ui5r *Sony_ActCount)
{
	tMacErr err = mnvm_miscErr;
	FILE *refnum = Drives[Drive_No];
	ui5r NewSony_Count = 0;

	if (0 == fseek(refnum, Sony_Start, SEEK_SET)) {
		if (IsWrite) {
			NewSony_Count = fwrite(Buffer, 1, Sony_Count, refnum);
		} else {
			NewSony_Count = fread(Buffer, 1, Sony_Count, refnum);
		}

		if (NewSony_Count == Sony_Count) {
			err = mnvm_noErr;
		}
	}

	if (nullpr != Sony_ActCount) {
		*Sony_ActCount = NewSony_Count;
	}

	return err; /*& figure out what really to return &*/
}

GLOBALFUNC tMacErr vSonyGetSize(tDrive Drive_No, ui5r *Sony_Count)
{
	tMacErr err = mnvm_miscErr;
	FILE *refnum = Drives[Drive_No];
	long v;

	if (0 == fseek(refnum, 0, SEEK_END)) {
		v = ftell(refnum);
		if (v >= 0) {
			*Sony_Count = v;
			err = mnvm_noErr;
		}
	}

	return err; /*& figure out what really to return &*/
}

LOCALFUNC tMacErr vSonyEject0(tDrive Drive_No, blnr deleteit)
{
	FILE *refnum = Drives[Drive_No];

	DiskEjectedNotify(Drive_No);

	fclose(refnum);
	Drives[Drive_No] = NotAfileRef; /* not really needed */

	return mnvm_noErr;
}

GLOBALFUNC tMacErr vSonyEject(tDrive Drive_No)
{
	return vSonyEject0(Drive_No, falseblnr);
}

LOCALPROC UnInitDrives(void)
{
	tDrive i;

	for (i = 0; i < NumDrives; ++i) {
		if (vSonyIsInserted(i)) {
			(void) vSonyEject(i);
		}
	}
}

LOCALFUNC blnr Sony_Insert0(FILE *refnum, blnr locked,
	char *drivepath)
{
	tDrive Drive_No;
	blnr IsOk = falseblnr;

	if (! FirstFreeDisk(&Drive_No)) {
		MacMsg(kStrTooManyImagesTitle, kStrTooManyImagesMessage,
			falseblnr);
	} else {
		/* printf("Sony_Insert0 %d\n", (int)Drive_No); */

		{
			Drives[Drive_No] = refnum;
			DiskInsertNotify(Drive_No, locked);

			IsOk = trueblnr;
		}
	}

	if (! IsOk) {
		fclose(refnum);
	}

	return IsOk;
}

LOCALFUNC blnr Sony_Insert1(char *drivepath, blnr silentfail)
{
	blnr locked = falseblnr;
	/* printf("Sony_Insert1 %s\n", drivepath); */
	FILE *refnum = fopen(drivepath, "rb+");
	if (NULL == refnum) {
		locked = trueblnr;
		refnum = fopen(drivepath, "rb");
	}
	if (NULL == refnum) {
		if (! silentfail) {
			MacMsg(kStrOpenFailTitle, kStrOpenFailMessage, falseblnr);
		}
	} else {
		return Sony_Insert0(refnum, locked, drivepath);
	}
	return falseblnr;
}

LOCALFUNC blnr Sony_Insert2(char *s)
{
	return Sony_Insert1(s, trueblnr);
}

LOCALFUNC blnr LoadInitialImages(void)
{
	if (! AnyDiskInserted()) {
		int n = NumDrives > 9 ? 9 : NumDrives;
		int i;
		char s[] = "disk?.dsk";

		for (i = 1; i <= n; ++i) {
			s[4] = '0' + i;
			if (! Sony_Insert2(s)) {
				/* stop on first error (including file not found) */
				return trueblnr;
			}
		}
	}

	return trueblnr;
}

/* --- ROM --- */

LOCALVAR char *rom_path = NULL;

LOCALFUNC tMacErr LoadMacRomFrom(char *path)
{
	tMacErr err;
	FILE *ROM_File;
	int File_Size;

	ROM_File = fopen(path, "rb");
	if (NULL == ROM_File) {
		err = mnvm_fnfErr;
	} else {
		File_Size = fread(ROM, 1, kROM_Size, ROM_File);
		if (File_Size != kROM_Size) {
			if (feof(ROM_File)) {
				err = mnvm_eofErr;
			} else {
				err = mnvm_miscErr;
			}
		} else {
			err = mnvm_noErr;
		}
		fclose(ROM_File);
	}

	return err;
}

LOCALFUNC blnr LoadMacRom(void)
{
	tMacErr err;

	if ((NULL == rom_path)
		|| (mnvm_fnfErr == (err = LoadMacRomFrom(rom_path))))
	if (mnvm_fnfErr == (err = LoadMacRomFrom(RomFileName)))
	{
	}

	if (mnvm_noErr != err) {
		if (mnvm_fnfErr == err) {
			MacMsg(kStrNoROMTitle, kStrNoROMMessage, trueblnr);
		} else if (mnvm_eofErr == err) {
			MacMsg(kStrShortROMTitle, kStrShortROMMessage,
				trueblnr);
		} else {
			MacMsg(kStrNoReadROMTitle, kStrNoReadROMMessage,
				trueblnr);
		}

		SpeedStopped = trueblnr;
	}

	return trueblnr; /* keep launching Mini vMac, regardless */
}

/* --- video out --- */

#if VarFullScreen
LOCALVAR blnr UseFullScreen = (WantInitFullScreen != 0);
#endif

#if EnableMagnify
LOCALVAR blnr UseMagnify = (WantInitMagnify != 0);
#endif

LOCALVAR blnr gBackgroundFlag = falseblnr;
LOCALVAR blnr gTrueBackgroundFlag = falseblnr;
LOCALVAR blnr CurSpeedStopped = trueblnr;

#if EnableMagnify
#define MaxScale MyWindowScale
#else
#define MaxScale 1
#endif

LOCALPROC HaveChangedScreenBuff(ui4r top, ui4r left,
	ui4r bottom, ui4r right)
{
    static int i = 0;
    u64 A = 0;
    u64 B = 0;
    
    A = osGetTime( );
#if vMacScreenDepth < 2
    //Video_UpdateTexture( ( u8* ) GetCurDrawBuff( ), vMacScreenWidth * vMacScreenHeight * 8, INFB_FORMAT_1BPP );
    Video_UpdateTexture2( ( u8* ) GetCurDrawBuff( ), vMacScreenWidth * ( bottom - top ) * 8, INFB_FORMAT_1BPP, top, bottom );
#endif
    B = osGetTime( );
    
    //printf( "%s took %dms\n", __FUNCTION__, ( int ) ( B - A ) );
}

LOCALPROC MyDrawChangesAndClear(void)
{
	if (ScreenChangedBottom > ScreenChangedTop) {
		HaveChangedScreenBuff(ScreenChangedTop, ScreenChangedLeft,
			ScreenChangedBottom, ScreenChangedRight);
		ScreenClearChanges();
	}
}

GLOBALPROC DoneWithDrawingForTick(void)
{
#if EnableMouseMotion && MayFullScreen
	if (HaveMouseMotion) {
		AutoScrollScreen();
	}
#endif
	MyDrawChangesAndClear();
}

/* --- mouse --- */

/* cursor hiding */

LOCALVAR blnr HaveCursorHidden = falseblnr;
LOCALVAR blnr WantCursorHidden = falseblnr;

LOCALPROC ForceShowCursor(void)
{
}

/* cursor moving */

LOCALFUNC blnr MyMoveMouse(si4b h, si4b v)
{
	return trueblnr;
}

/* cursor state */

LOCALPROC MousePositionNotify(int NewMousePosh, int NewMousePosv)
{
	blnr ShouldHaveCursorHidden = trueblnr;

#if EnableMagnify
	if (UseMagnify) {
		NewMousePosh /= MyWindowScale;
		NewMousePosv /= MyWindowScale;
	}
#endif

#if EnableMouseMotion && MayFullScreen
	if (HaveMouseMotion) {
		MyMousePositionSetDelta(NewMousePosh,
			NewMousePosv);
		SavedMouseH = NewMousePosh;
		SavedMouseV = NewMousePosv;
       // printf( "%s: %d, %d\n", __FUNCTION__, NewMousePosh, NewMousePosv );
	} else
#endif
	{
		if (NewMousePosh < 0) {
			NewMousePosh = 0;
			ShouldHaveCursorHidden = falseblnr;
		} else if (NewMousePosh >= vMacScreenWidth) {
			NewMousePosh = vMacScreenWidth - 1;
			ShouldHaveCursorHidden = falseblnr;
		}
		if (NewMousePosv < 0) {
			NewMousePosv = 0;
			ShouldHaveCursorHidden = falseblnr;
		} else if (NewMousePosv >= vMacScreenHeight) {
			NewMousePosv = vMacScreenHeight - 1;
			ShouldHaveCursorHidden = falseblnr;
		}

#if VarFullScreen
		if (UseFullScreen)
#endif
#if MayFullScreen
		{
			ShouldHaveCursorHidden = trueblnr;
		}
#endif

		/* if (ShouldHaveCursorHidden || CurMouseButton) */
		/*
			for a game like arkanoid, would like mouse to still
			move even when outside window in one direction
		*/
		MyMousePositionSet(NewMousePosh, NewMousePosv);
	}

	WantCursorHidden = ShouldHaveCursorHidden;
}

LOCALPROC CheckMouseState(void)
{
}

/* --- keyboard input --- */


LOCALPROC DisableKeyRepeat(void)
{
}

LOCALPROC RestoreKeyRepeat(void)
{
}

LOCALPROC ReconnectKeyCodes3(void)
{
}

LOCALPROC DisconnectKeyCodes3(void)
{
	DisconnectKeyCodes2();
	MyMouseButtonSet(falseblnr);
}

/* --- time, date, location --- */

#define dbglog_TimeStuff (0 && dbglog_HAVE)

LOCALVAR ui5b TrueEmulatedTime = 0;

#define MyInvTimeDivPow 16
#define MyInvTimeDiv (1 << MyInvTimeDivPow)
#define MyInvTimeDivMask (MyInvTimeDiv - 1)
#define MyInvTimeStep 1089590 /* 1000 / 60.14742 * MyInvTimeDiv */

typedef unsigned int Uint32;

LOCALVAR Uint32 LastTime;

LOCALVAR Uint32 NextIntTime;
LOCALVAR ui5b NextFracTime;

LOCALPROC IncrNextTime(void)
{
	NextFracTime += MyInvTimeStep;
	NextIntTime += (NextFracTime >> MyInvTimeDivPow);
	NextFracTime &= MyInvTimeDivMask;
}

LOCALPROC InitNextTime(void)
{
	NextIntTime = LastTime;
	NextFracTime = 0;
	IncrNextTime();
}

LOCALVAR ui5b NewMacDateInSeconds;

u64 MSAtAppStart = 0;

/*
 * Returns the time in milliseconds since the start of the app
 */
LOCALFUNC u32 GetMS( void ) {
    return ( u32 ) ( osGetTime( ) - MSAtAppStart );
}

LOCALFUNC blnr UpdateTrueEmulatedTime(void)
{
	Uint32 LatestTime;
	si5b TimeDiff;

    LatestTime = GetMS( ); //osGetTime( );
	if (LatestTime != LastTime) {

		NewMacDateInSeconds = LatestTime / 1000;
			/* no date and time api in SDL */

		LastTime = LatestTime;
		TimeDiff = (LatestTime - NextIntTime);
			/* this should work even when time wraps */
		if (TimeDiff >= 0) {
			if (TimeDiff > 256) {
				/* emulation interrupted, forget it */
				++TrueEmulatedTime;
				InitNextTime();

#if dbglog_TimeStuff
				dbglog_writelnNum("emulation interrupted",
					TrueEmulatedTime);
#endif
			} else {
				do {
					++TrueEmulatedTime;
					IncrNextTime();
					TimeDiff = (LatestTime - NextIntTime);
				} while (TimeDiff >= 0);
			}
			return trueblnr;
		} else {
			if (TimeDiff < -256) {
#if dbglog_TimeStuff
				dbglog_writeln("clock set back");
#endif
				/* clock goofed if ever get here, reset */
				InitNextTime();
			}
		}
	}
	return falseblnr;
}


LOCALFUNC blnr CheckDateTime(void)
{
	if (CurMacDateInSeconds != NewMacDateInSeconds) {
		CurMacDateInSeconds = NewMacDateInSeconds;
		return trueblnr;
	} else {
		return falseblnr;
	}
}

LOCALPROC StartUpTimeAdjust(void)
{
    LastTime = GetMS( );//osGetTime( );
	InitNextTime();
}

LOCALFUNC blnr InitLocationDat(void)
{
    LastTime = GetMS( ); //osGetTime( );
	InitNextTime();
	NewMacDateInSeconds = LastTime / 1000;
	CurMacDateInSeconds = NewMacDateInSeconds;

	return trueblnr;
}

/* --- sound --- */

#if MySoundEnabled

#define kLn2SoundBuffers 4 /* kSoundBuffers must be a power of two */
#define kSoundBuffers (1 << kLn2SoundBuffers)
#define kSoundBuffMask (kSoundBuffers - 1)

#define DesiredMinFilledSoundBuffs 3
	/*
		if too big then sound lags behind emulation.
		if too small then sound will have pauses.
	*/

#define kLnOneBuffLen 9
#define kLnAllBuffLen (kLn2SoundBuffers + kLnOneBuffLen)
#define kOneBuffLen (1UL << kLnOneBuffLen)
#define kAllBuffLen (1UL << kLnAllBuffLen)
#define kLnOneBuffSz (kLnOneBuffLen + kLn2SoundSampSz - 3)
#define kLnAllBuffSz (kLnAllBuffLen + kLn2SoundSampSz - 3)
#define kOneBuffSz (1UL << kLnOneBuffSz)
#define kAllBuffSz (1UL << kLnAllBuffSz)
#define kOneBuffMask (kOneBuffLen - 1)
#define kAllBuffMask (kAllBuffLen - 1)
#define dbhBufferSize (kAllBuffSz + kOneBuffSz)

#define dbglog_SoundStuff (0 && dbglog_HAVE)
#define dbglog_SoundBuffStats (0 && dbglog_HAVE)

LOCALVAR tpSoundSamp TheSoundBuffer = nullpr;
volatile static ui4b ThePlayOffset;
volatile static ui4b TheFillOffset;
volatile static ui4b MinFilledSoundBuffs;
#if dbglog_SoundBuffStats
LOCALVAR ui4b MaxFilledSoundBuffs;
#endif
LOCALVAR ui4b TheWriteOffset;

LOCALPROC MySound_Init0(void)
{
	ThePlayOffset = 0;
	TheFillOffset = 0;
	TheWriteOffset = 0;
}

LOCALPROC MySound_Start0(void)
{
	/* Reset variables */
	MinFilledSoundBuffs = kSoundBuffers + 1;
#if dbglog_SoundBuffStats
	MaxFilledSoundBuffs = 0;
#endif
}

GLOBALFUNC tpSoundSamp MySound_BeginWrite(ui4r n, ui4r *actL)
{
	ui4b ToFillLen = kAllBuffLen - (TheWriteOffset - ThePlayOffset);
	ui4b WriteBuffContig =
		kOneBuffLen - (TheWriteOffset & kOneBuffMask);

	if (WriteBuffContig < n) {
		n = WriteBuffContig;
	}
	if (ToFillLen < n) {
		/* overwrite previous buffer */
#if dbglog_SoundStuff
		dbglog_writeln("sound buffer over flow");
#endif
		TheWriteOffset -= kOneBuffLen;
	}

	*actL = n;
	return TheSoundBuffer + (TheWriteOffset & kAllBuffMask);
}

#if 4 == kLn2SoundSampSz
LOCALPROC ConvertSoundBlockToNative(tpSoundSamp p)
{
	int i;

	for (i = kOneBuffLen; --i >= 0; ) {
		*p++ -= 0x8000;
	}
}
#else
#define ConvertSoundBlockToNative(p)
#endif

LOCALPROC MySound_WroteABlock(void)
{
#if (4 == kLn2SoundSampSz)
	ui4b PrevWriteOffset = TheWriteOffset - kOneBuffLen;
	tpSoundSamp p = TheSoundBuffer + (PrevWriteOffset & kAllBuffMask);
#endif

#if dbglog_SoundStuff
	dbglog_writeln("enter MySound_WroteABlock");
#endif

	ConvertSoundBlockToNative(p);

	TheFillOffset = TheWriteOffset;

#if dbglog_SoundBuffStats
	{
		ui4b ToPlayLen = TheFillOffset
			- ThePlayOffset;
		ui4b ToPlayBuffs = ToPlayLen >> kLnOneBuffLen;

		if (ToPlayBuffs > MaxFilledSoundBuffs) {
			MaxFilledSoundBuffs = ToPlayBuffs;
		}
	}
#endif
}

LOCALFUNC blnr MySound_EndWrite0(ui4r actL)
{
	blnr v;

	TheWriteOffset += actL;

	if (0 != (TheWriteOffset & kOneBuffMask)) {
		v = falseblnr;
	} else {
		/* just finished a block */

		MySound_WroteABlock();

		v = trueblnr;
	}

	return v;
}

LOCALPROC MySound_SecondNotify0(void)
{
	if (MinFilledSoundBuffs <= kSoundBuffers) {
		if (MinFilledSoundBuffs > DesiredMinFilledSoundBuffs) {
#if dbglog_SoundStuff
			dbglog_writeln("MinFilledSoundBuffs too high");
#endif
			IncrNextTime();
		} else if (MinFilledSoundBuffs < DesiredMinFilledSoundBuffs) {
#if dbglog_SoundStuff
			dbglog_writeln("MinFilledSoundBuffs too low");
#endif
			++TrueEmulatedTime;
		}
#if dbglog_SoundBuffStats
		dbglog_writelnNum("MinFilledSoundBuffs",
			MinFilledSoundBuffs);
		dbglog_writelnNum("MaxFilledSoundBuffs",
			MaxFilledSoundBuffs);
		MaxFilledSoundBuffs = 0;
#endif
		MinFilledSoundBuffs = kSoundBuffers + 1;
	}
}

typedef ui4r trSoundTemp;

#define kCenterTempSound 0x8000

#define AudioStepVal 0x0040

#if 3 == kLn2SoundSampSz
#define ConvertTempSoundSampleFromNative(v) ((v) << 8)
#elif 4 == kLn2SoundSampSz
#define ConvertTempSoundSampleFromNative(v) ((v) + kCenterSound)
#else
#error "unsupported kLn2SoundSampSz"
#endif

#if 3 == kLn2SoundSampSz
#define ConvertTempSoundSampleToNative(v) ((v) >> 8)
#elif 4 == kLn2SoundSampSz
#define ConvertTempSoundSampleToNative(v) ((v) - kCenterSound)
#else
#error "unsupported kLn2SoundSampSz"
#endif

LOCALPROC SoundRampTo(trSoundTemp *last_val, trSoundTemp dst_val,
	tpSoundSamp *stream, int *len)
{
	trSoundTemp diff;
	tpSoundSamp p = *stream;
	int n = *len;
	trSoundTemp v1 = *last_val;

	while ((v1 != dst_val) && (0 != n)) {
		if (v1 > dst_val) {
			diff = v1 - dst_val;
			if (diff > AudioStepVal) {
				v1 -= AudioStepVal;
			} else {
				v1 = dst_val;
			}
		} else {
			diff = dst_val - v1;
			if (diff > AudioStepVal) {
				v1 += AudioStepVal;
			} else {
				v1 = dst_val;
			}
		}

		--n;
		*p++ = ConvertTempSoundSampleToNative(v1);
	}

	*stream = p;
	*len = n;
	*last_val = v1;
}

struct MySoundR {
	tpSoundSamp fTheSoundBuffer;
	volatile ui4b (*fPlayOffset);
	volatile ui4b (*fFillOffset);
	volatile ui4b (*fMinFilledSoundBuffs);

	volatile trSoundTemp lastv;

	blnr wantplaying;
	blnr HaveStartedPlaying;
};
typedef struct MySoundR MySoundR;

static void my_audio_callback(void *udata, Uint8 *stream, int len)
{
	ui4b ToPlayLen;
	ui4b FilledSoundBuffs;
	int i;
	MySoundR *datp = (MySoundR *)udata;
	tpSoundSamp CurSoundBuffer = datp->fTheSoundBuffer;
	ui4b CurPlayOffset = *datp->fPlayOffset;
	trSoundTemp v0 = datp->lastv;
	trSoundTemp v1 = v0;
	tpSoundSamp dst = (tpSoundSamp)stream;

#if kLn2SoundSampSz > 3
	len >>= (kLn2SoundSampSz - 3);
#endif

#if dbglog_SoundStuff
	dbglog_writeln("Enter my_audio_callback");
	dbglog_writelnNum("len", len);
#endif

label_retry:
	ToPlayLen = *datp->fFillOffset - CurPlayOffset;
	FilledSoundBuffs = ToPlayLen >> kLnOneBuffLen;

	if (! datp->wantplaying) {
#if dbglog_SoundStuff
		dbglog_writeln("playing end transistion");
#endif

		SoundRampTo(&v1, kCenterTempSound, &dst, &len);

		ToPlayLen = 0;
	} else if (! datp->HaveStartedPlaying) {
#if dbglog_SoundStuff
		dbglog_writeln("playing start block");
#endif

		if ((ToPlayLen >> kLnOneBuffLen) < 8) {
			ToPlayLen = 0;
		} else {
			tpSoundSamp p = datp->fTheSoundBuffer
				+ (CurPlayOffset & kAllBuffMask);
			trSoundTemp v2 = ConvertTempSoundSampleFromNative(*p);

#if dbglog_SoundStuff
			dbglog_writeln("have enough samples to start");
#endif

			SoundRampTo(&v1, v2, &dst, &len);

			if (v1 == v2) {
#if dbglog_SoundStuff
				dbglog_writeln("finished start transition");
#endif

				datp->HaveStartedPlaying = trueblnr;
			}
		}
	}

	if (0 == len) {
		/* done */

		if (FilledSoundBuffs < *datp->fMinFilledSoundBuffs) {
			*datp->fMinFilledSoundBuffs = FilledSoundBuffs;
		}
	} else if (0 == ToPlayLen) {

#if dbglog_SoundStuff
		dbglog_writeln("under run");
#endif

		for (i = 0; i < len; ++i) {
			*dst++ = ConvertTempSoundSampleToNative(v1);
		}
		*datp->fMinFilledSoundBuffs = 0;
	} else {
		ui4b PlayBuffContig = kAllBuffLen
			- (CurPlayOffset & kAllBuffMask);
		tpSoundSamp p = CurSoundBuffer
			+ (CurPlayOffset & kAllBuffMask);

		if (ToPlayLen > PlayBuffContig) {
			ToPlayLen = PlayBuffContig;
		}
		if (ToPlayLen > len) {
			ToPlayLen = len;
		}

		for (i = 0; i < ToPlayLen; ++i) {
			*dst++ = *p++;
		}
		v1 = ConvertTempSoundSampleFromNative(p[-1]);

		CurPlayOffset += ToPlayLen;
		len -= ToPlayLen;

		*datp->fPlayOffset = CurPlayOffset;

		goto label_retry;
	}

	datp->lastv = v1;
}

LOCALVAR MySoundR cur_audio;

LOCALVAR blnr HaveSoundOut = falseblnr;

LOCALPROC MySound_Stop(void)
{
#if dbglog_SoundStuff
	dbglog_writeln("enter MySound_Stop");
#endif

	if (cur_audio.wantplaying && HaveSoundOut) {
		ui4r retry_limit = 50; /* half of a second */

		cur_audio.wantplaying = falseblnr;

label_retry:
		if (kCenterTempSound == cur_audio.lastv) {
#if dbglog_SoundStuff
			dbglog_writeln("reached kCenterTempSound");
#endif

			/* done */
		} else if (0 == --retry_limit) {
#if dbglog_SoundStuff
			dbglog_writeln("retry limit reached");
#endif
			/* done */
		} else
		{
			/*
				give time back, particularly important
				if got here on a suspend event.
			*/

#if dbglog_SoundStuff
			dbglog_writeln("busy, so sleep");
#endif

			(void) SDL_Delay(10);

			goto label_retry;
		}

		SDL_PauseAudio(1);
	}

#if dbglog_SoundStuff
	dbglog_writeln("leave MySound_Stop");
#endif
}

LOCALPROC MySound_Start(void)
{
	if ((! cur_audio.wantplaying) && HaveSoundOut) {
		MySound_Start0();
		cur_audio.lastv = kCenterTempSound;
		cur_audio.HaveStartedPlaying = falseblnr;
		cur_audio.wantplaying = trueblnr;

		SDL_PauseAudio(0);
	}
}

LOCALPROC MySound_UnInit(void)
{
	if (HaveSoundOut) {
		SDL_CloseAudio();
	}
}

#define SOUND_SAMPLERATE 22255 /* = round(7833600 * 2 / 704) */

LOCALFUNC blnr MySound_Init(void)
{
	SDL_AudioSpec desired;

	MySound_Init0();

	cur_audio.fTheSoundBuffer = TheSoundBuffer;
	cur_audio.fPlayOffset = &ThePlayOffset;
	cur_audio.fFillOffset = &TheFillOffset;
	cur_audio.fMinFilledSoundBuffs = &MinFilledSoundBuffs;
	cur_audio.wantplaying = falseblnr;

	desired.freq = SOUND_SAMPLERATE;

#if 3 == kLn2SoundSampSz
	desired.format = AUDIO_U8;
#elif 4 == kLn2SoundSampSz
	desired.format = AUDIO_S16SYS;
#else
#error "unsupported audio format"
#endif

	desired.channels = 1;
	desired.samples = 1024;
	desired.callback = my_audio_callback;
	desired.userdata = (void *)&cur_audio;

	/* Open the audio device */
	if (SDL_OpenAudio(&desired, NULL) < 0) {
		fprintf(stderr, "Couldn't open audio: %s\n", SDL_GetError());
	} else {
		HaveSoundOut = trueblnr;

		MySound_Start();
			/*
				This should be taken care of by LeaveSpeedStopped,
				but since takes a while to get going properly,
				start early.
			*/
	}

	return trueblnr; /* keep going, even if no sound */
}

GLOBALPROC MySound_EndWrite(ui4r actL)
{
	if (MySound_EndWrite0(actL)) {
	}
}

LOCALPROC MySound_SecondNotify(void)
{
	if (HaveSoundOut) {
		MySound_SecondNotify0();
	}
}

#endif

/* --- basic dialogs --- */

LOCALPROC CheckSavedMacMsg(void)
{
	/* called only on quit, if error saved but not yet reported */

	if (nullpr != SavedBriefMsg) {
		char briefMsg0[ClStrMaxLength + 1];
		char longMsg0[ClStrMaxLength + 1];

		NativeStrFromCStr(briefMsg0, SavedBriefMsg);
		NativeStrFromCStr(longMsg0, SavedLongMsg);

		fprintf(stderr, "%s\n", briefMsg0);
		fprintf(stderr, "%s\n", longMsg0);

		SavedBriefMsg = nullpr;
	}
}

/* --- clipboard --- */

#define UseMotionEvents 1

#if UseMotionEvents
LOCALVAR blnr CaughtMouse = falseblnr;
#endif    

u32 Keys_Down = 0;
u32 Keys_Up = 0;
u32 Keys_Held = 0;

/*
 * TODO:
 * Read using analogue stick values.
 */
LOCALFUNC blnr GetCPadDelta( int* DeltaX, int* DeltaY ) {
    if ( Keys_Held & KEY_CPAD_LEFT ) *DeltaX = -3;
    if ( Keys_Held & KEY_CPAD_RIGHT ) *DeltaX = 3;
    
    if ( Keys_Held & KEY_CPAD_UP ) *DeltaY = -3;
    if ( Keys_Held & KEY_CPAD_DOWN ) *DeltaY = 3;
    
    return ( *DeltaX != 0 ) || ( *DeltaY != 0 ) ? trueblnr : falseblnr;
}

LOCALFUNC blnr GetTouchDelta( int* DeltaX, int* DeltaY ) {
    static touchPosition LastTP = {
        0,
        0
    };
    touchPosition TP;
    
    if ( Keys_Held & KEY_TOUCH ) {
        touchRead( &TP );
    
        *DeltaX = ( TP.px - LastTP.px );
        *DeltaY = ( TP.py - LastTP.py );
    
        LastTP = TP;
        
        return trueblnr;
    } else {
        *DeltaX = 0;
        *DeltaY = 0;
        
        LastTP.px = 0;
        LastTP.py = 0;
    }
    
    return falseblnr;
}

LOCALFUNC blnr IsMouseKeyDown( void ) {
    return ( Keys_Held & KEY_L ) || ( Keys_Held & KEY_R ) ? trueblnr : falseblnr;
}

LOCALPROC HandleMouseMovement( void ) {
    int MouseDeltaX = 0;
    int MouseDeltaY = 0;
    
    /*
     * Mouse input order:
     *
     * Touchscreen -> CPad -> CPadPro (TODO)
     */
    if ( GetTouchDelta( &MouseDeltaX, &MouseDeltaY ) ) {
        HaveMouseMotion = trueblnr;
    }
    else if ( GetCPadDelta( &MouseDeltaX, &MouseDeltaY ) ) {
        HaveMouseMotion = trueblnr;
    } else {
        HaveMouseMotion = falseblnr;
    }
    
    if ( HaveMouseMotion ) {
        /* Clamp the range of deltas so that the mouse doesn't go too fast/crazy */
        if ( MouseDeltaX < -10 ) MouseDeltaX = -10;
        if ( MouseDeltaX > 10 ) MouseDeltaX = 10;
        
        if ( MouseDeltaY < -10 ) MouseDeltaY = -10;
        if ( MouseDeltaY > 10 ) MouseDeltaY = 10;
        
        MyMousePositionSetDelta( MouseDeltaX, MouseDeltaY );
        
        //printf( "dx: %d dy: %d\n", MouseDeltaX, MouseDeltaY );
    }
}

typedef enum {
    ScaleMode_1to1 = 0,     // No scaling applied
    ScaleMode_FitToWidth,   // Scale to fill the screen horizontally
    ScaleMode_FitToHeight,  // Scale to fill the screen vertically
    ScaleMode_Stretch,      // Stretch display to fit screen horizontally and vertically
    NumScaleModes
} ScreenScaleMode;

#define ScreenCenterX ( MyScreenWidth / 2 )
#define ScreenCenterY ( MyScreenHeight / 2 )

#define MacScreenCenterX ( vMacScreenWidth / 2 )
#define MacScreenCenterY ( vMacScreenHeight / 2 )

int ScreenScrollX = 0;
int ScreenScrollY = 0;

ScreenScaleMode ScaleMode = ScaleMode_1to1;

/* Screen scale factors */
float ScreenScaleW = 1.0f;
float ScreenScaleH = 1.0f;

LOCALPROC ToggleScreenScaleMode( void ) {
    ScaleMode++;
    
    if ( ScaleMode >= NumScaleModes )
        ScaleMode = ScaleMode_1to1;
    
    switch ( ScaleMode ) {
        case ScaleMode_1to1: {
            ScreenScaleW = 1.0f;
            ScreenScaleH = 1.0f;
            
            break;
        }
        case ScaleMode_FitToWidth: {
            ScreenScaleW = ( float ) MyScreenWidth / ( float ) vMacScreenWidth;
            ScreenScaleH = ( float ) MyScreenWidth / ( float ) vMacScreenWidth;
            
            break;
        }
        case ScaleMode_FitToHeight: {
            ScreenScaleW = ( float ) MyScreenHeight / ( float ) vMacScreenHeight;
            ScreenScaleH = ( float ) MyScreenHeight / ( float ) vMacScreenHeight;
            
            break;
        }
        case ScaleMode_Stretch: {
            ScreenScaleW = ( float ) MyScreenWidth / ( float ) vMacScreenWidth;
            ScreenScaleH = ( float ) MyScreenHeight / ( float ) vMacScreenHeight;
            
            break;
        }
        default: {
            ScreenScaleW = 1.0f;
            ScreenScaleH = 1.0f;
            
            break;
        }
    }
    
    /* Linear filtering makes unscaled mode look like crap for some reason.
     * Disable it for this scale mode only.
     */
    if ( ScaleMode == ScaleMode_1to1 ) C3D_TexSetFilter( &FBTexture, GPU_NEAREST, GPU_NEAREST );
    else C3D_TexSetFilter( &FBTexture, GPU_LINEAR, GPU_LINEAR );
    
    /* Reset scrolling offsets */
    ScreenScrollX = 0;
    ScreenScrollY = 0;
    
    // printf( "m: %d w: %.1f h: %.1f\n", ScaleMode, ScreenScaleW, ScreenScaleH );
}

/*
 * TODO:
 * The scroll offset in one of the scale modes is wrong.
 */
LOCALPROC UpdateScreenScroll( void ) {
    float MaxScrollX = ( ( ( float ) vMacScreenWidth ) * ScreenScaleW ) - MyScreenWidth;
    float MaxScrollY = ( ( ( float ) vMacScreenHeight ) * ScreenScaleH ) - MyScreenHeight;
    
    ScreenScrollX = ( ( MyScreenWidth / 2 ) - CurMouseH );
    ScreenScrollY = ( ( MyScreenHeight / 2 ) - CurMouseV );
    
    /* Clamp to the edges of the screen */
    if ( ScreenScrollX > 0 ) ScreenScrollX = 0;
    if ( ScreenScrollX < -MaxScrollX ) ScreenScrollX = -MaxScrollX;
    
    if ( ScreenScrollY < -MaxScrollY ) ScreenScrollY = -MaxScrollY;
    if ( ScreenScrollY > 0 ) ScreenScrollY = 0;
    
    //printf( "SW: %d SH: %d\n", ScreenScrollX, ScreenScrollY );
}

/* --- event handling for main window --- */

LOCALPROC HandleTheEvent( void ) {
    if ( aptMainLoop( ) ) {
        hidScanInput( );
        
        Keys_Down = hidKeysDown( );
        Keys_Up = hidKeysUp( );
        Keys_Held = hidKeysHeld( );
        
        HandleMouseMovement( );
        MyMouseButtonSet( IsMouseKeyDown( ) );
        
        /*
         * HACKHACKHACK
         *
         * For the love of god do this properly.
         */
        if ( Keys_Down & KEY_START )
            ForceMacOff = trueblnr;
        
        if ( Keys_Down & KEY_SELECT )
            ToggleScreenScaleMode( );
        
        UpdateScreenScroll( );
        
        C3D_FrameBegin( C3D_FRAME_SYNCDRAW );
           FB_Draw( MainRenderTarget, ScreenScrollX, ScreenScrollY, ScreenScaleW, ScreenScaleH );
        C3D_FrameEnd( 0 );
        
        // printf( "dx: %d, dy: %d\n", dx, dy );
        
        //printf( "%d, %d\n", MouseX, MouseY );
        //printf( "time: %d\n", osGetTime( ) );
    }
}

/* --- main window creation and disposal --- */

LOCALVAR int my_argc;
LOCALVAR char **my_argv;

LOCALFUNC blnr Screen_Init(void)
{
    return trueblnr;
}

#if MayFullScreen
LOCALVAR blnr GrabMachine = falseblnr;
#endif

#if MayFullScreen
LOCALPROC GrabTheMachine(void)
{
}
#endif

#if MayFullScreen
LOCALPROC UngrabMachine(void)
{
}
#endif

#if EnableMouseMotion && MayFullScreen
LOCALPROC MyMouseConstrain(void)
{
}
#endif

LOCALFUNC blnr CreateMainWindow(void)
{
    return trueblnr;
}

#if EnableMagnify || VarFullScreen
LOCALFUNC blnr ReCreateMainWindow(void)
{
	return trueblnr;
}
#endif

LOCALPROC ZapWinStateVars(void)
{
}

#if VarFullScreen
LOCALPROC ToggleWantFullScreen(void)
{
	WantFullScreen = ! WantFullScreen;
}
#endif

/* --- SavedTasks --- */

LOCALPROC LeaveBackground(void)
{
	ReconnectKeyCodes3();
	DisableKeyRepeat();
}

LOCALPROC EnterBackground(void)
{
	RestoreKeyRepeat();
	DisconnectKeyCodes3();

	ForceShowCursor();
}

LOCALPROC LeaveSpeedStopped(void)
{
#if MySoundEnabled
	MySound_Start();
#endif

	StartUpTimeAdjust();
}

LOCALPROC EnterSpeedStopped(void)
{
#if MySoundEnabled
	MySound_Stop();
#endif
}

LOCALPROC CheckForSavedTasks(void)
{
	if (MyEvtQNeedRecover) {
		MyEvtQNeedRecover = falseblnr;

		/* attempt cleanup, MyEvtQNeedRecover may get set again */
		MyEvtQTryRecoverFromFull();
	}

#if EnableMouseMotion && MayFullScreen
	if (HaveMouseMotion) {
		MyMouseConstrain();
	}
#endif

	if (RequestMacOff) {
		RequestMacOff = falseblnr;
		if (AnyDiskInserted()) {
			MacMsgOverride(kStrQuitWarningTitle,
				kStrQuitWarningMessage);
		} else {
			ForceMacOff = trueblnr;
		}
	}

	if (ForceMacOff) {
		return;
	}

	if (gTrueBackgroundFlag != gBackgroundFlag) {
		gBackgroundFlag = gTrueBackgroundFlag;
		if (gTrueBackgroundFlag) {
			EnterBackground();
		} else {
			LeaveBackground();
		}
	}

	if (CurSpeedStopped != (SpeedStopped ||
		(gBackgroundFlag && ! RunInBackground
#if EnableAutoSlow && 0
			&& (QuietSubTicks >= 4092)
#endif
		)))
	{
		CurSpeedStopped = ! CurSpeedStopped;
		if (CurSpeedStopped) {
			EnterSpeedStopped();
		} else {
			LeaveSpeedStopped();
		}
	}

	if ((nullpr != SavedBriefMsg) & ! MacMsgDisplayed) {
		MacMsgDisplayOn();
	}

#if EnableMagnify || VarFullScreen
	if (0
#if EnableMagnify
		|| (UseMagnify != WantMagnify)
#endif
#if VarFullScreen
		|| (UseFullScreen != WantFullScreen)
#endif
		)
	{
		(void) ReCreateMainWindow();
	}
#endif

#if MayFullScreen
	if (GrabMachine != (
#if VarFullScreen
		UseFullScreen &&
#endif
		! (gTrueBackgroundFlag || CurSpeedStopped)))
	{
		GrabMachine = ! GrabMachine;
		if (GrabMachine) {
			GrabTheMachine();
		} else {
			UngrabMachine();
		}
	}
#endif

	if (NeedWholeScreenDraw) {
		NeedWholeScreenDraw = falseblnr;
		ScreenChangedAll();
	}
}

/* --- command line parsing --- */

LOCALFUNC blnr ScanCommandLine(void)
{
	return trueblnr;
}

/* --- main program flow --- */

GLOBALFUNC blnr ExtraTimeNotOver(void)
{
	UpdateTrueEmulatedTime();
	return TrueEmulatedTime == OnTrueTime;
}

LOCALPROC WaitForTheNextEvent(void)
{
}

LOCALPROC CheckForSystemEvents(void)
{
    HandleTheEvent( );
}

void MyDelay( u32 TimeToDelay ) {
    u64 TimeWhenDone = osGetTime( ) + TimeToDelay;
    
    while ( osGetTime( ) < TimeWhenDone );
}

GLOBALPROC WaitForNextTick(void)
{
label_retry:
	CheckForSystemEvents();
	CheckForSavedTasks();

	if (ForceMacOff) {
		return;
	}

	if (CurSpeedStopped) {
		DoneWithDrawingForTick();
		WaitForTheNextEvent();
		goto label_retry;
	}

	if (ExtraTimeNotOver()) {
		MyDelay(NextIntTime - LastTime);
		goto label_retry;
	}

	if (CheckDateTime()) {
#if MySoundEnabled
		MySound_SecondNotify();
#endif
#if EnableDemoMsg
		DemoModeSecondNotify();
#endif
	}

	if ((! gBackgroundFlag)
#if UseMotionEvents
		&& (! CaughtMouse)
#endif
		)
	{
		CheckMouseState();
	}

	OnTrueTime = TrueEmulatedTime;

#if dbglog_TimeStuff
	dbglog_writelnNum("WaitForNextTick, OnTrueTime", OnTrueTime);
#endif
}

/* --- platform independent code can be thought of as going here --- */

#include "PROGMAIN.h"

LOCALPROC ZapOSGLUVars(void)
{
	InitDrives();
	ZapWinStateVars();
}

LOCALPROC ReserveAllocAll(void)
{
#if dbglog_HAVE
	dbglog_ReserveAlloc();
#endif
	ReserveAllocOneBlock(&ROM, kROM_Size, 5, falseblnr);

	ReserveAllocOneBlock(&screencomparebuff,
		vMacScreenNumBytes, 5, trueblnr);
#if UseControlKeys
	ReserveAllocOneBlock(&CntrlDisplayBuff,
		vMacScreenNumBytes, 5, falseblnr);
#endif

#if MySoundEnabled
	ReserveAllocOneBlock((ui3p *)&TheSoundBuffer,
		dbhBufferSize, 5, falseblnr);
#endif

	EmulationReserveAlloc();
}

LOCALFUNC blnr AllocMyMemory(void)
{
	uimr n;
	blnr IsOk = falseblnr;

	ReserveAllocOffset = 0;
	ReserveAllocBigBlock = nullpr;
	ReserveAllocAll();
	n = ReserveAllocOffset;
	ReserveAllocBigBlock = (ui3p)calloc(1, n);
	if (NULL == ReserveAllocBigBlock) {
		MacMsg(kStrOutOfMemTitle, kStrOutOfMemMessage, trueblnr);
	} else {
		ReserveAllocOffset = 0;
		ReserveAllocAll();
		if (n != ReserveAllocOffset) {
			/* oops, program error */
		} else {
			IsOk = trueblnr;
		}
	}

	return IsOk;
}

LOCALPROC UnallocMyMemory(void)
{
	if (nullpr != ReserveAllocBigBlock) {
		free((char *)ReserveAllocBigBlock);
	}
}

LOCALFUNC blnr InitOSGLU(void)
{
    chdir( "sdmc:/3ds/vmac/" );
    
    MSAtAppStart = osGetTime( );
    
    if ( Video_Init( ) )
	if (AllocMyMemory())
#if dbglog_HAVE
	if (dbglog_open())
#endif
	if (ScanCommandLine())
	if (LoadInitialImages())
	if (LoadMacRom())
	if (InitLocationDat())
#if MySoundEnabled
	if (MySound_Init())
#endif
	if (Screen_Init())
	if (CreateMainWindow())
	{
        printf( "A\n" );
		return trueblnr;
	}
    printf( "B\n" );
	return falseblnr;
}

LOCALPROC UnInitOSGLU(void)
{
	if (MacMsgDisplayed) {
		MacMsgDisplayOff();
	}

	RestoreKeyRepeat();
#if MayFullScreen
	UngrabMachine();
#endif
#if MySoundEnabled
	MySound_Stop();
#endif
#if MySoundEnabled
	MySound_UnInit();
#endif
#if IncludePbufs
	UnInitPbufs();
#endif
	UnInitDrives();

	ForceShowCursor();

#if dbglog_HAVE
	dbglog_close();
#endif

	UnallocMyMemory();

	CheckSavedMacMsg();

    Video_Close( );
}

int main(int argc, char **argv)
{
	my_argc = argc;
	my_argv = argv;

	ZapOSGLUVars();
	if (InitOSGLU()) {
		ProgramMain();
	}
	UnInitOSGLU();

	return 0;
}
