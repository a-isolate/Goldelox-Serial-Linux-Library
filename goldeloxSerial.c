

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>

#include "Goldelox_Types4D.h"			// defines data types used by the 4D Routines
#include "Goldelox_const4D.h"			// defines for 4dgl constants, generated by conversion of 4DGL constants to target language

#define   Err4D_OK              0
#define   Err4D_Timeout         1
#define   Err4D_NAK		2               // other than ACK received


// 4D Global variables
int    cPort;                                   // comp port handle, used by Intrinsic routines
int    Error4D ;  				// Error indicator,  used and set by Intrinsic routines
unsigned char Error4D_Inv ;                     // Error byte returned from com port, onl set if error = Err_Invalid
int Error_Abort4D ;                             // if true routines will abort when detecting an error
int TimeLimit4D ;                               // time limit in ms for total serial command duration, 2000 (2 seconds) should be adequate for most commands
                                                // assuming a reasonable baud rate AND low latency AND 0 for the Serial Delay Parameter
                                                // temporary increase might be required for very long (bitmap write, large image file opens)
int(*Callback4D) (int, unsigned char) ;         // or indeterminate (eg file_exec, file_run, file_callFunction)  commands

/*
 * Start of 4D Intrinsic Routines
*/

void WriteBytes(unsigned char *psOutput, int nCount)
{
    int iOut;
    // Quick return if no device
    if (cPort < 0)
    {
        Error4D = Err4D_OK;
        return;
    }
    iOut = write(cPort, psOutput, nCount);
    if (iOut < 0)
    {
        printf("write error %d %s\n", errno, strerror(errno));
    }
    if (iOut != nCount)
        printf("Write incomplete!\n");
    return;
}

void WriteChars(unsigned char *psOutput)
{
    // Include NULL in output
    WriteBytes(psOutput, strlen((char *)psOutput) + 1);
    return;
}

void WriteWords(WORD * Source, int Size)
{
 	WORD wk ;
	int i ;
	for (i = 0; i < Size; i++)
	{
		wk = *Source++ ;
		wk = (wk >> 8) + (wk << 8) ;
		WriteBytes((unsigned char *)&wk, 2);
	}
}

// Return system time in ms
DWORD GetTickCount(void)
{
    struct timespec ttime;
    clock_gettime(CLOCK_MONOTONIC, &ttime);
    return (ttime.tv_sec * 1000) + (ttime.tv_nsec / 1000000);
}

// read string from the serial port
// return code:
//   >= 0 = number of characters read
//   -1 = read failed
int ReadSerPort(unsigned char *psData, int iMax)
{
    int iIn, iLeft, iIdx;
    DWORD sttime;

    // Quick return if no device
    if (cPort < 0)
    {
        Error4D = Err4D_OK;
        *psData = '\0';
        return iMax;
    }
    iIdx = 0;
    iLeft = iMax;
    sttime = GetTickCount();
    while (iLeft > 0)
    {
        iIn = read(cPort, &psData[iIdx], iLeft);
        if (iIn < 0)
        {
            if (errno == EAGAIN)
            {
                // Would block -- check timeout
                if ((GetTickCount() - sttime) >= TimeLimit4D)
                {
                    //printf("timeout - %d read\n", iIdx);
                    return -(iIdx + 10000);
                }
                usleep(100);  // Wait .1ms
                continue;
            }
            printf("Read error %d %s\n", errno, strerror(errno));
            return -1;
        }
        // Anything?
        if (iIn > 0)
        {
            // Calc remaining
            iLeft -= iIn;
            iIdx += iIn;
        }
        // Keep reading
    }

    return iMax;
}

void getbytes(unsigned char *data, int size)
{
 	int readc;
	readc = ReadSerPort(data, size) ;
	if ((readc != size)
	    && (Callback4D != NULL) )
	{
		Error4D = Err4D_Timeout ;
		Callback4D(Error4D, Error4D_Inv) ;
 	}
}

void GetAck(void)
{
	int readc;
	unsigned char readx ;
	Error4D = Err4D_OK ;
    // Quick return if no device
    if (cPort < 0)
    {
        Error4D = Err4D_OK;
        return;
    }
   	readc = ReadSerPort(&readx, 1) ;

	if (readc != 1)
    {
		Error4D = Err4D_Timeout ;
        if (Callback4D != NULL)
            Callback4D(Error4D, Error4D_Inv) ;
    }
	else if (readx != 6)
	{
	   	Error4D     = Err4D_NAK ;
	   	Error4D_Inv = readx ;
		if (Callback4D != NULL)
	 		Callback4D(Error4D, Error4D_Inv) ;
	}

    return;
}


WORD GetWord(void)
{
 	int readc;
 	unsigned char readx[2] ;

 	if (Error4D != Err4D_OK)
 		return 0 ;

    readc = ReadSerPort(&readx[0], 2) ;

	if (readc != 2)
	{
		Error4D  = Err4D_Timeout ;
		if (Callback4D != NULL)
	 		return Callback4D(Error4D, Error4D_Inv) ;
		return -Error4D ;
	}
	else
		return readx[0] << 8 | readx[1] ;
}

void getString(unsigned char *outStr, int strLen)
{
 	int readc;

 	if (Error4D != Err4D_OK)
	{
		outStr[0] = '\0' ;
 		return ;
	}

	readc = ReadSerPort(outStr, strLen) ;

	if (readc != strLen)
	{
		Error4D  = Err4D_Timeout ;
		if (Callback4D != NULL)
	 		Callback4D(Error4D, Error4D_Inv) ;
	}

    // Append EOS
	outStr[readc] = '\0' ;

	return;
}

WORD GetAckResp(void)
{
	GetAck() ;
	return GetWord() ;
}

WORD WaitForAck(void)
{
    int saveTimeout = TimeLimit4D;
    void *saveCB = Callback4D;

    // check once per minute
    Callback4D = NULL;
    TimeLimit4D = 60 * 1000;
    do
    {
        GetAck();
    } while (Error4D != Err4D_OK);

    // Restore callback/timeout saves
    TimeLimit4D = saveTimeout;
    Callback4D = saveCB;

    return GetWord();
}
WORD GetAckRes2Words(WORD * word1, WORD * word2)
{
	int Result ;
	GetAck() ;
	Result = GetWord() ;
	*word1 = GetWord() ;
	*word2 = GetWord() ;
	return Result ;
}

void GetAck2Words(WORD * word1, WORD * word2)
{
	GetAck() ;
	*word1 = GetWord() ;
	*word2 = GetWord() ;
}

WORD GetAckResSector(t4DSector Sector)
{
	int Result;
	GetAck() ;
	Result = GetWord() ;
	getbytes(Sector, 512) ;
	return Result ;
}

WORD GetAckResStr(unsigned char * OutStr)
{
	int Result ;
	GetAck() ;
	Result = GetWord() ;
	getString(OutStr, Result) ;
	return Result ;
}

WORD GetAckResData(t4DByteArray OutData, WORD size)
{
	int Result ;
	GetAck() ;
	Result = GetWord() ;
	getbytes(OutData, size) ;
	return Result ;
}

/*
 * End Of Intrinsic 4DRoutines here
*/

/*
 * Starts of 4D Compound Routines
*/

WORD charheight(unsigned char  TestChar)
{
  unsigned char  towrite[3] ;
  towrite[0]= F_charheight >> 8 ;
  towrite[1]= F_charheight ;
  towrite[2]= TestChar;
  WriteBytes(towrite, 3) ;
  return GetAckResp() ;
}

WORD charwidth(unsigned char  TestChar)
{
  unsigned char  towrite[3] ;
  towrite[0]= F_charwidth >> 8 ;
  towrite[1]= F_charwidth ;
  towrite[2]= TestChar;
  WriteBytes(towrite, 3) ;
  return GetAckResp() ;
}

void gfx_BGcolour(WORD  Color)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_gfx_BGcolour >> 8 ;
  towrite[1]= F_gfx_BGcolour & 0xFF;
  towrite[2]= Color >> 8 ;
  towrite[3]= Color ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void gfx_ChangeColour(WORD  OldColor, WORD  NewColor)
{
  unsigned char  towrite[6] ;
  towrite[0]= F_gfx_ChangeColour >> 8 ;
  towrite[1]= F_gfx_ChangeColour ;
  towrite[2]= OldColor >> 8 ;
  towrite[3]= OldColor ;
  towrite[4]= NewColor >> 8 ;
  towrite[5]= NewColor ;
  WriteBytes(towrite, 6) ;
  GetAck() ;
}

void gfx_Circle(WORD  X, WORD  Y, WORD  Radius, WORD  Color)
{
  unsigned char  towrite[10] ;
  towrite[0]= F_gfx_Circle >> 8 ;
  towrite[1]= F_gfx_Circle ;
  towrite[2]= X >> 8 ;
  towrite[3]= X ;
  towrite[4]= Y >> 8 ;
  towrite[5]= Y ;
  towrite[6]= Radius >> 8 ;
  towrite[7]= Radius ;
  towrite[8]= Color >> 8 ;
  towrite[9]= Color ;
  WriteBytes(towrite, 10) ;
  GetAck() ;
}

void gfx_CircleFilled(WORD  X, WORD  Y, WORD  Radius, WORD  Color)
{
  unsigned char  towrite[10] ;
  towrite[0]= F_gfx_CircleFilled >> 8 ;
  towrite[1]= F_gfx_CircleFilled ;
  towrite[2]= X >> 8 ;
  towrite[3]= X ;
  towrite[4]= Y >> 8 ;
  towrite[5]= Y ;
  towrite[6]= Radius >> 8 ;
  towrite[7]= Radius ;
  towrite[8]= Color >> 8 ;
  towrite[9]= Color ;
  WriteBytes(towrite, 10) ;
  GetAck() ;
}

void gfx_Clipping(WORD  OnOff)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_gfx_Clipping >> 8 ;
  towrite[1]= F_gfx_Clipping & 0xFF;
  towrite[2]= OnOff >> 8 ;
  towrite[3]= OnOff ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void gfx_ClipWindow(WORD  X1, WORD  Y1, WORD  X2, WORD  Y2)
{
  unsigned char  towrite[10] ;

  towrite[0]= F_gfx_ClipWindow >> 8 ;
  towrite[1]= F_gfx_ClipWindow ;
  towrite[2]= X1 >> 8 ;
  towrite[3]= X1 ;
  towrite[4]= Y1 >> 8 ;
  towrite[5]= Y1 ;
  towrite[6]= X2 >> 8 ;
  towrite[7]= X2 ;
  towrite[8]= Y2 >> 8 ;
  towrite[9]= Y2 ;
  WriteBytes(towrite, 10) ;
  GetAck() ;
}

void gfx_Cls()
{
  unsigned char  towrite[2] ;

  towrite[0]= F_gfx_Cls >> 8 ;
  towrite[1]= F_gfx_Cls ;
  WriteBytes(towrite, 2) ;
  GetAck() ;
}

void gfx_Contrast(WORD  Contrast)
{
  unsigned char  towrite[4] ;
    towrite[0]= F_gfx_Contrast >> 8 ;
  towrite[1]= F_gfx_Contrast & 0xFF;
  towrite[2]= Contrast >> 8 ;
  towrite[3]= Contrast ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void gfx_FrameDelay(WORD  Msec)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_gfx_FrameDelay >> 8 ;
  towrite[1]= F_gfx_FrameDelay & 0xFF;
  towrite[2]= Msec >> 8 ;
  towrite[3]= Msec ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

WORD gfx_GetPixel(WORD  X, WORD  Y)
{
  unsigned char  towrite[6] ;
  towrite[0]= F_gfx_GetPixel >> 8 ;
  towrite[1]= F_gfx_GetPixel ;
  towrite[2]= X >> 8 ;
  towrite[3]= X ;
  towrite[4]= Y >> 8 ;
  towrite[5]= Y ;
  WriteBytes(towrite, 6) ;
  return GetAckResp() ;
}

void gfx_Line(WORD  X1, WORD  Y1, WORD  X2, WORD  Y2, WORD  Color)
{
  unsigned char  towrite[12] ;
  towrite[0]= F_gfx_Line >> 8 ;
  towrite[1]= F_gfx_Line ;
  towrite[2]= X1 >> 8 ;
  towrite[3]= X1 ;
  towrite[4]= Y1 >> 8 ;
  towrite[5]= Y1 ;
  towrite[6]= X2 >> 8 ;
  towrite[7]= X2 ;
  towrite[8]= Y2 >> 8 ;
  towrite[9]= Y2 ;
  towrite[10]= Color >> 8 ;
  towrite[11]= Color ;
  WriteBytes(towrite, 12) ;
  GetAck() ;
}

void gfx_LinePattern(WORD  Pattern)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_gfx_LinePattern >> 8 ;
  towrite[1]= F_gfx_LinePattern & 0xFF;
  towrite[2]= Pattern >> 8 ;
  towrite[3]= Pattern ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void gfx_LineTo(WORD  X, WORD  Y)
{
  unsigned char  towrite[6] ;
  towrite[0]= F_gfx_LineTo >> 8 ;
  towrite[1]= F_gfx_LineTo ;
  towrite[2]= X >> 8 ;
  towrite[3]= X ;
  towrite[4]= Y >> 8 ;
  towrite[5]= Y ;
  WriteBytes(towrite, 6) ;
  GetAck() ;
}

void gfx_MoveTo(WORD  X, WORD  Y)
{
  unsigned char  towrite[6] ;
  towrite[0]= F_gfx_MoveTo >> 8 ;
  towrite[1]= F_gfx_MoveTo ;
  towrite[2]= X >> 8 ;
  towrite[3]= X ;
  towrite[4]= Y >> 8 ;
  towrite[5]= Y ;
  WriteBytes(towrite, 6) ;
  GetAck() ;
}

WORD gfx_Orbit(WORD  Angle, WORD  Distance, WORD *  Xdest, WORD *  Ydest)
{
  unsigned char  towrite[6] ;
  towrite[0]= F_gfx_Orbit >> 8 ;
  towrite[1]= F_gfx_Orbit ;
  towrite[2]= Angle >> 8 ;
  towrite[3]= Angle ;
  towrite[4]= Distance >> 8 ;
  towrite[5]= Distance ;
  WriteBytes(towrite, 6) ;
  GetAck2Words(Xdest,Ydest) ;
  return 0 ;
}

void gfx_OutlineColour(WORD  Color)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_gfx_OutlineColour >> 8 ;
  towrite[1]= F_gfx_OutlineColour & 0xFF;
  towrite[2]= Color >> 8 ;
  towrite[3]= Color ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void gfx_Polygon(WORD  n, t4DWordArray  Xvalues, t4DWordArray  Yvalues, WORD  Color)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_gfx_Polygon >> 8 ;
  towrite[1]= F_gfx_Polygon ;
  towrite[2]= n >> 8 ;
  towrite[3]= n ;
  WriteBytes(towrite, 4) ;
  WriteWords(Xvalues, n) ;
  WriteWords(Yvalues, n) ;
  towrite[0]= Color >> 8 ;
  towrite[1]= Color ;
  WriteBytes(towrite, 2) ;
  GetAck() ;
}

void gfx_Polyline(WORD  n, t4DWordArray  Xvalues, t4DWordArray  Yvalues, WORD  Color)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_gfx_Polyline >> 8 ;
  towrite[1]= F_gfx_Polyline ;
  towrite[2]= n >> 8 ;
  towrite[3]= n ;
  WriteBytes(towrite, 4) ;
  WriteWords(Xvalues, n) ;
  WriteWords(Yvalues, n) ;
  towrite[0]= Color >> 8 ;
  towrite[1]= Color ;
  WriteBytes(towrite, 2) ;
  GetAck() ;
}

void gfx_PutPixel(WORD  X, WORD  Y, WORD  Color)
{
  unsigned char  towrite[8] ;
  towrite[0]= F_gfx_PutPixel >> 8 ;
  towrite[1]= F_gfx_PutPixel ;
  towrite[2]= X >> 8 ;
  towrite[3]= X ;
  towrite[4]= Y >> 8 ;
  towrite[5]= Y ;
  towrite[6]= Color >> 8 ;
  towrite[7]= Color ;
  WriteBytes(towrite, 8) ;
  GetAck() ;
}

void gfx_Rectangle(WORD  X1, WORD  Y1, WORD  X2, WORD  Y2, WORD  Color)
{
  unsigned char  towrite[12] ;
  towrite[0]= F_gfx_Rectangle >> 8 ;
  towrite[1]= F_gfx_Rectangle ;
  towrite[2]= X1 >> 8 ;
  towrite[3]= X1 ;
  towrite[4]= Y1 >> 8 ;
  towrite[5]= Y1 ;
  towrite[6]= X2 >> 8 ;
  towrite[7]= X2 ;
  towrite[8]= Y2 >> 8 ;
  towrite[9]= Y2 ;
  towrite[10]= Color >> 8 ;
  towrite[11]= Color ;
  WriteBytes(towrite, 12) ;
  GetAck() ;
}

void gfx_RectangleFilled(WORD  X1, WORD  Y1, WORD  X2, WORD  Y2, WORD  Color)
{
  unsigned char  towrite[12] ;
  towrite[0]= F_gfx_RectangleFilled >> 8 ;
  towrite[1]= F_gfx_RectangleFilled ;
  towrite[2]= X1 >> 8 ;
  towrite[3]= X1 ;
  towrite[4]= Y1 >> 8 ;
  towrite[5]= Y1 ;
  towrite[6]= X2 >> 8 ;
  towrite[7]= X2 ;
  towrite[8]= Y2 >> 8 ;
  towrite[9]= Y2 ;
  towrite[10]= Color >> 8 ;
  towrite[11]= Color ;
  WriteBytes(towrite, 12) ;
  GetAck() ;
}

void gfx_ScreenMode(WORD  ScreenMode)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_gfx_ScreenMode >> 8 ;
  towrite[1]= F_gfx_ScreenMode & 0xFF;
  towrite[2]= ScreenMode >> 8 ;
  towrite[3]= ScreenMode ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void gfx_Set(WORD  Func, WORD  Value)
{
  unsigned char  towrite[6] ;
  towrite[0]= F_gfx_Set >> 8 ;
  towrite[1]= F_gfx_Set ;
  towrite[2]= Func >> 8 ;
  towrite[3]= Func ;
  towrite[4]= Value >> 8 ;
  towrite[5]= Value ;
  WriteBytes(towrite, 6) ;
  GetAck() ;
}

void gfx_SetClipRegion()
{
  unsigned char  towrite[2] ;
  towrite[0]= F_gfx_SetClipRegion >> 8 ;
  towrite[1]= F_gfx_SetClipRegion ;
  WriteBytes(towrite, 2) ;
  GetAck() ;
}

void gfx_Transparency(WORD  OnOff)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_gfx_Transparency >> 8 ;
  towrite[1]= F_gfx_Transparency & 0xFF;
  towrite[2]= OnOff >> 8 ;
  towrite[3]= OnOff ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void gfx_TransparentColour(WORD  Color)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_gfx_TransparentColour >> 8 ;
  towrite[1]= F_gfx_TransparentColour & 0xFF;
  towrite[2]= Color >> 8 ;
  towrite[3]= Color ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void gfx_Triangle(WORD  X1, WORD  Y1, WORD  X2, WORD  Y2, WORD  X3, WORD  Y3, WORD  Color)
{
  unsigned char  towrite[16] ;
  towrite[0]= F_gfx_Triangle >> 8 ;
  towrite[1]= F_gfx_Triangle ;
  towrite[2]= X1 >> 8 ;
  towrite[3]= X1 ;
  towrite[4]= Y1 >> 8 ;
  towrite[5]= Y1 ;
  towrite[6]= X2 >> 8 ;
  towrite[7]= X2 ;
  towrite[8]= Y2 >> 8 ;
  towrite[9]= Y2 ;
  towrite[10]= X3 >> 8 ;
  towrite[11]= X3 ;
  towrite[12]= Y3 >> 8 ;
  towrite[13]= Y3 ;
  towrite[14]= Color >> 8 ;
  towrite[15]= Color ;
  WriteBytes(towrite, 16) ;
  GetAck() ;
}

WORD media_Flush()
{
  unsigned char  towrite[2] ;
  towrite[0]= F_media_Flush >> 8 ;
  towrite[1]= F_media_Flush ;
  WriteBytes(towrite, 2) ;
  return GetAckResp() ;
}

void media_Image(WORD  X, WORD  Y)
{
  unsigned char  towrite[6] ;
  towrite[0]= F_media_Image >> 8 ;
  towrite[1]= F_media_Image ;
  towrite[2]= X >> 8 ;
  towrite[3]= X ;
  towrite[4]= Y >> 8 ;
  towrite[5]= Y ;
  WriteBytes(towrite, 6) ;
  GetAck() ;
}

WORD media_Init()
{
  unsigned char  towrite[2] ;
  towrite[0]= F_media_Init >> 8 ;
  towrite[1]= F_media_Init ;
  WriteBytes(towrite, 2) ;
  return GetAckResp() ;
}

WORD media_ReadByte()
{
  unsigned char  towrite[2] ;
  towrite[0]= F_media_ReadByte >> 8 ;
  towrite[1]= F_media_ReadByte ;
  WriteBytes(towrite, 2) ;
  return GetAckResp() ;
}

WORD media_ReadWord()
{
  unsigned char  towrite[2] ;
  towrite[0]= F_media_ReadWord >> 8 ;
  towrite[1]= F_media_ReadWord ;
  WriteBytes(towrite, 2) ;
  return GetAckResp() ;
}

void media_SetAdd(WORD  HiWord, WORD  LoWord)
{
  unsigned char  towrite[6] ;
  towrite[0]= F_media_SetAdd >> 8 ;
  towrite[1]= F_media_SetAdd ;
  towrite[2]= HiWord >> 8 ;
  towrite[3]= HiWord ;
  towrite[4]= LoWord >> 8 ;
  towrite[5]= LoWord ;
  WriteBytes(towrite, 6) ;
  GetAck() ;
}

void media_SetSector(WORD  HiWord, WORD  LoWord)
{
  unsigned char  towrite[6] ;
  towrite[0]= F_media_SetSector >> 8 ;
  towrite[1]= F_media_SetSector ;
  towrite[2]= HiWord >> 8 ;
  towrite[3]= HiWord ;
  towrite[4]= LoWord >> 8 ;
  towrite[5]= LoWord ;
  WriteBytes(towrite, 6) ;
  GetAck() ;
}

void media_Video(WORD  X, WORD  Y)
{
  unsigned char  towrite[6] ;
  towrite[0]= F_media_Video >> 8 ;
  towrite[1]= F_media_Video ;
  towrite[2]= X >> 8 ;
  towrite[3]= X ;
  towrite[4]= Y >> 8 ;
  towrite[5]= Y ;
  WriteBytes(towrite, 6) ;
  GetAck() ;
}

void media_VideoFrame(WORD  X, WORD  Y, WORD  Framenumber)
{
  unsigned char  towrite[8] ;
  towrite[0]= F_media_VideoFrame >> 8 ;
  towrite[1]= F_media_VideoFrame ;
  towrite[2]= X >> 8 ;
  towrite[3]= X ;
  towrite[4]= Y >> 8 ;
  towrite[5]= Y ;
  towrite[6]= Framenumber >> 8 ;
  towrite[7]= Framenumber ;
  WriteBytes(towrite, 8) ;
  GetAck() ;
}

WORD media_WriteByte(WORD  Byte)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_media_WriteByte >> 8 ;
  towrite[1]= F_media_WriteByte ;
  towrite[2]= Byte >> 8 ;
  towrite[3]= Byte ;
  WriteBytes(towrite, 4) ;
  return GetAckResp() ;
}

WORD media_WriteWord(WORD  Word)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_media_WriteWord >> 8 ;
  towrite[1]= F_media_WriteWord ;
  towrite[2]= Word >> 8 ;
  towrite[3]= Word ;
  WriteBytes(towrite, 4) ;
  return GetAckResp() ;
}

void putCH(WORD  WordChar)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_putCH >> 8 ;
  towrite[1]= F_putCH ;
  towrite[2]= WordChar >> 8 ;
  towrite[3]= WordChar ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void putstr(unsigned char *  InString)
{
  unsigned char  towrite[2] ;
  towrite[0]= F_putstr >> 8 ;
  towrite[1]= F_putstr ;
  WriteBytes(towrite, 2) ;
  WriteChars(InString) ;
  GetAck() ;
}

void txt_Attributes(WORD  Attribs)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_txt_Attributes >> 8 ;
  towrite[1]= F_txt_Attributes & 0xFF;
  towrite[2]= Attribs >> 8 ;
  towrite[3]= Attribs ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void txt_BGcolour(WORD  Color)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_txt_BGcolour >> 8 ;
  towrite[1]= F_txt_BGcolour & 0xFF;
  towrite[2]= Color >> 8 ;
  towrite[3]= Color ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void txt_Bold(WORD  Bold)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_txt_Bold >> 8 ;
  towrite[1]= F_txt_Bold & 0xFF;
  towrite[2]= Bold >> 8 ;
  towrite[3]= Bold ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void txt_FGcolour(WORD  Color)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_txt_FGcolour >> 8 ;
  towrite[1]= F_txt_FGcolour & 0xFF;
  towrite[2]= Color >> 8 ;
  towrite[3]= Color ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void txt_FontID(WORD  FontNumber)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_txt_FontID >> 8 ;
  towrite[1]= F_txt_FontID & 0xFF;
  towrite[2]= FontNumber >> 8 ;
  towrite[3]= FontNumber ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void txt_Height(WORD  Multiplier)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_txt_Height >> 8 ;
  towrite[1]= F_txt_Height & 0xFF;
  towrite[2]= Multiplier >> 8 ;
  towrite[3]= Multiplier ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void txt_Inverse(WORD  Inverse)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_txt_Inverse >> 8 ;
  towrite[1]= F_txt_Inverse & 0xFF;
  towrite[2]= Inverse >> 8 ;
  towrite[3]= Inverse ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void txt_Italic(WORD  Italic)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_txt_Italic >> 8 ;
  towrite[1]= F_txt_Italic & 0xFF;
  towrite[2]= Italic >> 8 ;
  towrite[3]= Italic ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void txt_MoveCursor(WORD  Line, WORD  Column)
{
  unsigned char  towrite[6] ;
  towrite[0]= F_txt_MoveCursor >> 8 ;
  towrite[1]= F_txt_MoveCursor ;
  towrite[2]= Line >> 8 ;
  towrite[3]= Line ;
  towrite[4]= Column >> 8 ;
  towrite[5]= Column ;
  WriteBytes(towrite, 6) ;
  GetAck() ;
}

void txt_Opacity(WORD  TransparentOpaque)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_txt_Opacity >> 8 ;
  towrite[1]= F_txt_Opacity & 0xFF;
  towrite[2]= TransparentOpaque >> 8 ;
  towrite[3]= TransparentOpaque ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void txt_Set(WORD  Func, WORD  Value)
{
  unsigned char  towrite[6] ;
  towrite[0]= F_txt_Set >> 8 ;
  towrite[1]= F_txt_Set ;
  towrite[2]= Func >> 8 ;
  towrite[3]= Func ;
  towrite[4]= Value >> 8 ;
  towrite[5]= Value ;
  WriteBytes(towrite, 6) ;
  GetAck() ;
}

void txt_Underline(WORD  Underline)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_txt_Underline >> 8 ;
  towrite[1]= F_txt_Underline & 0xFF;
  towrite[2]= Underline >> 8 ;
  towrite[3]= Underline ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void txt_Width(WORD  Multiplier)
{
  unsigned char  towrite[4] ;

  towrite[0]= F_txt_Width >> 8 ;
  towrite[1]= F_txt_Width & 0xFF;
  towrite[2]= Multiplier >> 8 ;
  towrite[3]= Multiplier ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void txt_Xgap(WORD  Pixels)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_txt_Xgap >> 8 ;
  towrite[1]= F_txt_Xgap & 0xFF;
  towrite[2]= Pixels >> 8 ;
  towrite[3]= Pixels ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void txt_Ygap(WORD  Pixels)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_txt_Ygap >> 8 ;
  towrite[1]= F_txt_Ygap & 0xFF;
  towrite[2]= Pixels >> 8 ;
  towrite[3]= Pixels ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void BeeP(WORD  Note, WORD  Duration)
{
  unsigned char  towrite[6] ;
  towrite[0]= F_BeeP >> 8 ;
  towrite[1]= F_BeeP ;
  towrite[2]= Note >> 8 ;
  towrite[3]= Note ;
  towrite[4]= Duration >> 8 ;
  towrite[5]= Duration ;
  WriteBytes(towrite, 6) ;
  GetAck() ;
}

WORD sys_GetModel(unsigned char *  ModelStr)
{
  unsigned char  towrite[2] ;
  towrite[0]= F_sys_GetModel >> 8 ;
  towrite[1]= F_sys_GetModel ;
  WriteBytes(towrite, 2) ;
  return GetAckResStr(ModelStr) ;
}

WORD sys_GetVersion()
{
  unsigned char  towrite[2] ;
  towrite[0]= F_sys_GetVersion >> 8 ;
  towrite[1]= F_sys_GetVersion ;
  WriteBytes(towrite, 2) ;
  return GetAckResp() ;
}

WORD sys_GetPmmC()
{
  unsigned char  towrite[2] ;
  towrite[0]= F_sys_GetPmmC >> 8 ;
  towrite[1]= F_sys_GetPmmC ;
  WriteBytes(towrite, 2) ;
  return GetAckResp() ;
}

void blitComtoDisplay(WORD  X, WORD  Y, WORD  Width, WORD  Height, t4DByteArray  Pixels)
{
  unsigned char  towrite[10] ;
  towrite[0]= F_blitComtoDisplay >> 8 ;
  towrite[1]= F_blitComtoDisplay ;
  towrite[2]= X >> 8 ;
  towrite[3]= X ;
  towrite[4]= Y >> 8 ;
  towrite[5]= Y ;
  towrite[6]= Width >> 8 ;
  towrite[7]= Width ;
  towrite[8]= Height >> 8 ;
  towrite[9]= Height ;
  WriteBytes(towrite, 10) ;
  WriteBytes(Pixels, Width*Height*2) ;
  GetAck() ;
}

void setbaudWait(WORD  Newrate)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_setbaudWait >> 8 ;
  towrite[1]= F_setbaudWait ;
  towrite[2]= Newrate >> 8 ;
  towrite[3]= Newrate ;
  WriteBytes(towrite, 4) ;
  //SetThisBaudrate(Newrate) ; // change this systems baud rate to match new display rate, ACK is 100ms away
  GetAck() ;
}

WORD peekW(WORD  Address)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_peekW >> 8 ;
  towrite[1]= F_peekW ;
  towrite[2]= Address >> 8 ;
  towrite[3]= Address ;
  WriteBytes(towrite, 4) ;
  return GetAckResp() ;
}

void pokeW(WORD  Address, WORD  WordValue)
{
  unsigned char  towrite[6] ;
  towrite[0]= F_pokeW >> 8 ;
  towrite[1]= F_pokeW ;
  towrite[2]= Address >> 8 ;
  towrite[3]= Address ;
  towrite[4]= WordValue >> 8 ;
  towrite[5]= WordValue ;
  WriteBytes(towrite, 6) ;
  GetAck() ;
}

WORD peekB(WORD  Address)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_peekB >> 8 ;
  towrite[1]= F_peekB ;
  towrite[2]= Address >> 8 ;
  towrite[3]= Address ;
  WriteBytes(towrite, 4) ;
  return GetAckResp() ;
}

void pokeB(WORD  Address, WORD  ByteValue)
{
  unsigned char  towrite[6] ;
  towrite[0]= F_pokeB >> 8 ;
  towrite[1]= F_pokeB ;
  towrite[2]= Address >> 8 ;
  towrite[3]= Address ;
  towrite[4]= ByteValue >> 8 ;
  towrite[5]= ByteValue ;
  WriteBytes(towrite, 6) ;
  GetAck() ;
}

WORD joystick()
{
  unsigned char  towrite[2] ;
  towrite[0]= F_joystick >> 8 ;
  towrite[1]= F_joystick ;
  WriteBytes(towrite, 2) ;
  return GetAckResp() ;
}

void SSTimeout(WORD  Seconds)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_SSTimeout >> 8 ;
  towrite[1]= F_SSTimeout ;
  towrite[2]= Seconds >> 8 ;
  towrite[3]= Seconds ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void SSSpeed(WORD  Speed)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_SSSpeed >> 8 ;
  towrite[1]= F_SSSpeed ;
  towrite[2]= Speed >> 8 ;
  towrite[3]= Speed ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

void SSMode(WORD  Parm)
{
  unsigned char  towrite[4] ;
  towrite[0]= F_SSMode >> 8 ;
  towrite[1]= F_SSMode ;
  towrite[2]= Parm >> 8 ;
  towrite[3]= Parm ;
  WriteBytes(towrite, 4) ;
  GetAck() ;
}

/*
void setbaudWait(WORD  Newrate)
{
  unsigned char  towrite[4] ;

  towrite[0]= F_setbaudWait >> 8 ;
  towrite[1]= F_setbaudWait ;
  towrite[2]= Newrate >> 8 ;
  towrite[3]= Newrate ;
  WriteBytes(towrite, 4) ;
  //SetThisBaudrate(Newrate) ; // change this systems baud rate to match new display rate, ACK is 100ms away
  GetAck() ;
}
*/

/*
 * Conpound 4D Routines Ends here
*/


int OpenComm(char *sDeviceName, int newrate)
{
    struct termios new_port_settings;
    //WORD    nBaud;
    int k, ch, tSave, baudr;
    
switch(newrate)
  {
    case      50 : baudr = B50;         break;
    case      75 : baudr = B75;         break;
    case     110 : baudr = B110;        break;
    case     134 : baudr = B134;        break;
    case     150 : baudr = B150;        break;
    case     200 : baudr = B200;        break;
    case     300 : baudr = B300;        break;
    case     600 : baudr = B600;        break;
    case    1200 : baudr = B1200;       break;
    case    1800 : baudr = B1800;       break;
    case    2400 : baudr = B2400;       break;
    case    4800 : baudr = B4800;       break;
    case    9600 : baudr = B9600;       break;
    case   19200 : baudr = B19200;      break;
    case   38400 : baudr = B38400;      break;
    case   57600 : baudr = B57600;      break;
    case  115200 : baudr = B115200;     break;
    case  230400 : baudr = B230400;     break;
    case  460800 : baudr = B460800;     break;
    case  500000 : baudr = B500000;     break;
    case  576000 : baudr = B576000;     break;
    case  921600 : baudr = B921600;     break;
    case 1000000 : baudr = B1000000;    break;
    default      : printf("invalid baudrate\n");
                   return(1);
                   break;
}
    cPort = open(sDeviceName, O_RDWR | O_NOCTTY | O_NDELAY);
// Current config
    tcgetattr(cPort, &new_port_settings);
    // Set the line to RAW
    cfmakeraw(&new_port_settings);
    memset(&new_port_settings, 0, sizeof(new_port_settings));  /* clear the new struct */
    new_port_settings.c_cflag = baudr | CS8 | CLOCAL | CREAD;
    new_port_settings.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    new_port_settings.c_iflag = IGNPAR;
    new_port_settings.c_oflag = 0;
    new_port_settings.c_lflag = 0;
    new_port_settings.c_cc[VMIN] = 0;      /* block untill n bytes are received */
    new_port_settings.c_cc[VTIME] = 100;     /* block untill a timer expires (n * 100 mSec.) */
/*
    cfsetospeed(&new_port_settings, nBaud);
    cfsetispeed(&new_port_settings, nBaud);
*/
    // set new config
    tcsetattr(cPort, TCSANOW, &new_port_settings);
    // Set non-blocking
    fcntl(cPort, F_SETFL, FNDELAY);
    
    tSave = TimeLimit4D;
    TimeLimit4D = 500;
    for (k = 0 ; k < 10 ; k++)
    {
        ch = 'X';
        write(cPort, (unsigned char *)&ch, 1);
        tcflush(cPort, TCOFLUSH);
        ReadSerPort((unsigned char *)&ch, 1);
        if (ch == 0x15)
            break ;
    }
    TimeLimit4D = tSave;

    tcflush(cPort, TCIOFLUSH);
    return 0;
}

void CloseComm(void)
{
    close(cPort);
    cPort = -1;
    Error4D = Err4D_OK;

    return;
}
