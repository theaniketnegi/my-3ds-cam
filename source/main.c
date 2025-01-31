#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>
#include <setjmp.h>
#include <3ds.h>
#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <stdbool.h>

#define WAIT_TIMEOUT 300000000ULL

#define WIDTH 400
#define HEIGHT 240
#define SCREEN_SIZE WIDTH *HEIGHT * 2
#define BUF_SIZE SCREEN_SIZE * 2

static jmp_buf exitJmp;

inline void clearScreen(void)
{
	u8 *frame = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
	memset(frame, 0, 320 * 240 * 3);
}

void hang(char *message)
{
	clearScreen();
	printf("%s", message);
	printf("Press start to exit");

	while (aptMainLoop())
	{
		hidScanInput();

		u32 kHeld = hidKeysHeld();
		if (kHeld & KEY_START)
			longjmp(exitJmp, 1);
	}
}

void cleanup()
{
	camExit();
	gfxExit();
	acExit();
}

void writePictureToFramebufferRGB565(void *fb, void *img, u16 x, u16 y, u16 width, u16 height)
{
	u8 *fb_8 = (u8 *)fb;
	u16 *img_16 = (u16 *)img;
	int i, j, draw_x, draw_y;
	for (j = 0; j < height; j++)
	{
		for (i = 0; i < width; i++)
		{
			draw_y = y + height - j;
			draw_x = x + i;
			u32 v = (draw_y + draw_x * height) * 3;
			u16 data = img_16[j * width + i];
			uint8_t b = ((data >> 11) & 0x1F) << 3;
			uint8_t g = ((data >> 5) & 0x3F) << 2;
			uint8_t r = (data & 0x1F) << 3;
			fb_8[v] = r;
			fb_8[v + 1] = g;
			fb_8[v + 2] = b;
		}
	}
}

// TODO: Figure out how to use CAMU_GetStereoCameraCalibrationData
void takePicture3D(u8 *buf)
{
	u32 bufSize;
	printf("CAMU_GetMaxBytes: 0x%08X\n", (unsigned int)CAMU_GetMaxBytes(&bufSize, WIDTH, HEIGHT));
	printf("CAMU_SetTransferBytes: 0x%08X\n", (unsigned int)CAMU_SetTransferBytes(PORT_BOTH, bufSize, WIDTH, HEIGHT));

	printf("CAMU_Activate: 0x%08X\n", (unsigned int)CAMU_Activate(SELECT_OUT1_OUT2));

	Handle camReceiveEvent = 0;
	Handle camReceiveEvent2 = 0;

	printf("CAMU_ClearBuffer: 0x%08X\n", (unsigned int)CAMU_ClearBuffer(PORT_BOTH));
	printf("CAMU_SynchronizeVsyncTiming: 0x%08X\n", (unsigned int)CAMU_SynchronizeVsyncTiming(SELECT_OUT1, SELECT_OUT2));

	printf("CAMU_StartCapture: 0x%08X\n", (unsigned int)CAMU_StartCapture(PORT_BOTH));

	printf("CAMU_SetReceiving: 0x%08X\n", (unsigned int)CAMU_SetReceiving(&camReceiveEvent, buf, PORT_CAM1, SCREEN_SIZE, (s16)bufSize));
	printf("CAMU_SetReceiving: 0x%08X\n", (unsigned int)CAMU_SetReceiving(&camReceiveEvent2, buf + SCREEN_SIZE, PORT_CAM2, SCREEN_SIZE, (s16)bufSize));
	printf("svcWaitSynchronization: 0x%08X\n", (unsigned int)svcWaitSynchronization(camReceiveEvent, WAIT_TIMEOUT));
	printf("svcWaitSynchronization: 0x%08X\n", (unsigned int)svcWaitSynchronization(camReceiveEvent2, WAIT_TIMEOUT));

	printf("CAMU_StopCapture: 0x%08X\n", (unsigned int)CAMU_StopCapture(PORT_BOTH));

	svcCloseHandle(camReceiveEvent);
	svcCloseHandle(camReceiveEvent2);

	printf("CAMU_Activate: 0x%08X\n", (unsigned int)CAMU_Activate(SELECT_NONE));
}

void savePictureWithTimestamp(u8 *buffer, size_t size)
{
	// Get the current date and time
	time_t rawtime;
	struct tm *timeinfo;
	char timeStr[32];

	time(&rawtime);
	timeinfo = localtime(&rawtime);

	strftime(timeStr, sizeof(timeStr), "%Y-%m-%d_%H-%M-%S", timeinfo);

	mkdir("sdmc:/3DImages", 0777);

	char filename[256];
	snprintf(filename, sizeof(filename), "%s/%s.rgb565", "sdmc:/3DImages", timeStr);

	FILE *file = fopen(filename, "wb");
	if (!file)
	{
		printf("Failed to open file for writing: %s\n", filename);
		return;
	}

	size_t written = fwrite(buffer, 1, size, file);
	if (written != size)
	{
		printf("Failed to write data to file. Only %zu bytes written.\n", written);
	}
	else
	{
		printf("Image successfully saved to %s\n", filename);
	}

	fclose(file);
}

int main()
{
	// Initializations
	acInit();
	gfxInitDefault();
	consoleInit(GFX_BOTTOM, NULL);

	// Enable double buffering to remove screen tearing
	gfxSetDoubleBuffering(GFX_TOP, true);
	gfxSetDoubleBuffering(GFX_BOTTOM, false);

	// Save current stack frame for easy exit
	if (setjmp(exitJmp))
	{
		cleanup();
		return 0;
	}

	u32 kDown;
	u32 kHeld;

	printf("Initializing camera\n");

	printf("camInit: 0x%08X\n", (unsigned int)camInit());

	printf("CAMU_SetSize: 0x%08X\n", (unsigned int)CAMU_SetSize(SELECT_OUT1_OUT2, SIZE_CTR_TOP_LCD, CONTEXT_A));
	printf("CAMU_SetOutputFormat: 0x%08X\n", (unsigned int)CAMU_SetOutputFormat(SELECT_OUT1_OUT2, OUTPUT_RGB_565, CONTEXT_A));

	printf("CAMU_SetNoiseFilter: 0x%08X\n", (unsigned int)CAMU_SetNoiseFilter(SELECT_OUT1_OUT2, true));
	printf("CAMU_SetAutoExposure: 0x%08X\n", (unsigned int)CAMU_SetAutoExposure(SELECT_OUT1_OUT2, true));
	printf("CAMU_SetAutoWhiteBalance: 0x%08X\n", (unsigned int)CAMU_SetAutoWhiteBalance(SELECT_OUT1_OUT2, true));
	// printf("CAMU_SetEffect: 0x%08X\n", (unsigned int) CAMU_SetEffect(SELECT_OUT1_OUT2, EFFECT_MONO, CONTEXT_A));

	printf("CAMU_SetTrimming: 0x%08X\n", (unsigned int)CAMU_SetTrimming(PORT_CAM1, false));
	printf("CAMU_SetTrimming: 0x%08X\n", (unsigned int)CAMU_SetTrimming(PORT_CAM2, false));
	// printf("CAMU_SetTrimmingParamsCenter: 0x%08X\n", (unsigned int) CAMU_SetTrimmingParamsCenter(PORT_CAM1, 512, 240, 512, 384));

	u8 *buf = malloc(BUF_SIZE);
	if (!buf)
	{
		hang("Failed to allocate memory!");
	}

	gfxFlushBuffers();
	gspWaitForVBlank();
	gfxSwapBuffers();

	bool held_R = false;

	printf("\nPress R to take a new picture\n");
	printf("Press Start to exit to Homebrew Launcher\n");

	// Main loop
	while (aptMainLoop())
	{
		// Read which buttons are currently pressed or not
		hidScanInput();
		kDown = hidKeysDown();
		kHeld = hidKeysHeld();

		// If START button is pressed, break loop and quit
		if (kDown & KEY_START)
		{
			break;
		}

		Handle camReceiveEvent = 0;
		u32 bufSize;

		printf("CAMU_GetMaxBytes: 0x%08X\n", (unsigned int)CAMU_GetMaxBytes(&bufSize, WIDTH, HEIGHT));
		printf("CAMU_SetTransferBytes: 0x%08X\n", (unsigned int)CAMU_SetTransferBytes(PORT_CAM1, bufSize, WIDTH, HEIGHT));
		printf("CAMU_Activate: 0x%08X\n", (unsigned int)CAMU_Activate(SELECT_OUT1_OUT2));
		printf("CAMU_ClearBuffer: 0x%08X\n", (unsigned int)CAMU_ClearBuffer(PORT_CAM1));
		printf("CAMU_StartCapture: 0x%08X\n", (unsigned int)CAMU_StartCapture(PORT_CAM1));
		printf("CAMU_SetReceiving: 0x%08X\n", (unsigned int)CAMU_SetReceiving(&camReceiveEvent, buf, PORT_CAM1, SCREEN_SIZE, (s16)bufSize));
		printf("svcWaitSynchronization: 0x%08X\n", (unsigned int)svcWaitSynchronization(camReceiveEvent, WAIT_TIMEOUT));
		printf("CAMU_StopCapture: 0x%08X\n", (unsigned int)CAMU_StopCapture(PORT_CAM1));
		svcCloseHandle(camReceiveEvent);
		writePictureToFramebufferRGB565(gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), buf, 0, 0, WIDTH, HEIGHT);
		if ((kHeld & KEY_R) && !held_R)
		{
			printf("Capturing new image\n");
			gfxFlushBuffers();
			gspWaitForVBlank();
			gfxSwapBuffers();
			held_R = true;
			takePicture3D(buf);
			savePictureWithTimestamp(buf, BUF_SIZE);
		}
		else if (!(kHeld & KEY_R))
		{
			held_R = false;
		}

		// Flush and swap framebuffers
		gfxFlushBuffers();
		gspWaitForVBlank();
		gfxSwapBuffers();
	}

	// Exit
	free(buf);
	cleanup();

	// Return to hbmenu
	return 0;
}
