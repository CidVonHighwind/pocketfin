/* The sceMpeg imports, in place of -lpspmpeg: pspsdk does not declare
 * sceMpegGetAvcNalAu, which takes a raw H.264 access unit and SPS/PPS with no
 * PSMF container.
 *
 * Either of these wrong and the module fails to start on hardware while still
 * loading under emulation: IMPORT_START/IMPORT_FUNC from pspimport.s, not
 * STUB_START/STUB_FUNC from pspstub.s, and the version word 0x00090000. */

	.set noreorder

#include "pspimport.s"

	IMPORT_START "sceMpeg", 0x00090000

	IMPORT_FUNC  "sceMpeg", 0x682A619B, sceMpegInit
	IMPORT_FUNC  "sceMpeg", 0x874624D6, sceMpegFinish
	IMPORT_FUNC  "sceMpeg", 0xC132E22F, sceMpegQueryMemSize
	IMPORT_FUNC  "sceMpeg", 0xD8C5F121, sceMpegCreate
	IMPORT_FUNC  "sceMpeg", 0x606A4649, sceMpegDelete
	IMPORT_FUNC  "sceMpeg", 0x167AFD9E, sceMpegInitAu
	IMPORT_FUNC  "sceMpeg", 0x0E3C2E9D, sceMpegAvcDecode
	IMPORT_FUNC  "sceMpeg", 0x11F95CF1, sceMpegGetAvcNalAu
	IMPORT_FUNC  "sceMpeg", 0xA11C7026, sceMpegAvcDecodeMode
