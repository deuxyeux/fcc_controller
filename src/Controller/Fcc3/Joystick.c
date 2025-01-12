/*
LUFA Library
Copyright (C) Dean Camera, 2015.

dean [at] fourwalledcubicle [dot] com
www.lufa-lib.org
*/

/*
Copyright 2015  Dean Camera (dean [at] fourwalledcubicle [dot] com)

Permission to use, copy, modify, distribute, and sell this
software and its documentation for any purpose is hereby granted
without fee, provided that the above copyright notice appear in
all copies and that both that the copyright notice and this
permission notice and warranty disclaimer appear in supporting
documentation, and that the name of the author not be used in
advertising or publicity pertaining to distribution of the
software without specific, written prior permission.

The author disclaims all warranties with regard to this
software, including all implied warranties of merchantability
and fitness.  In no event shall the author be liable for any
special, indirect or consequential damages or any damages
whatsoever resulting from loss of use, data or profits, whether
in an action of contract, negligence or other tortious action,
arising out of or in connection with the use or performance of
this software.
*/

/** \file
*
*  Main source file for the Joystick demo. This file contains the main tasks of the demo and
*  is responsible for the initial application hardware configuration.
*/

#include "Joystick.h"

// Global bool for task running
// bool JoystickRunning = true;
/** Main program entry point. This routine configures the hardware required by the application, then
*  enters a loop to run the application tasks in sequence.
*/
int main(void)
{
	SetupHardware();

	LEDs_SetAllLEDs(LEDMASK_USB_NOTREADY);
	GlobalInterruptEnable();
	InitFCC();

	for (;;)
	{
		HID_Task();
		USB_USBTask();
	}
}

/** Configures the board hardware and chip peripherals for the demo's functionality. */
void SetupHardware(void)
{
	#if (ARCH == ARCH_AVR8)
	/* Disable watchdog if enabled by bootloader/fuses */
	MCUSR &= ~(1 << WDRF);
	wdt_disable();

	/* Disable clock division */
	clock_prescale_set(clock_div_1);
	#elif (ARCH == ARCH_XMEGA)
	/* Start the PLL to multiply the 2MHz RC oscillator to 32MHz and switch the CPU core to run from it */
	XMEGACLK_StartPLL(CLOCK_SRC_INT_RC2MHZ, 2000000, F_CPU);
	XMEGACLK_SetCPUClockSource(CLOCK_SRC_PLL);

	/* Start the 32MHz internal RC oscillator and start the DFLL to increase it to 48MHz using the USB SOF as a reference */
	XMEGACLK_StartInternalOscillator(CLOCK_SRC_INT_RC32MHZ);
	XMEGACLK_StartDFLL(CLOCK_SRC_INT_RC32MHZ, DFLL_REF_INT_USBSOF, F_USB);

	PMIC.CTRL = PMIC_LOLVLEN_bm | PMIC_MEDLVLEN_bm | PMIC_HILVLEN_bm;
	#endif
	//_delay_ms(2000);
	/* Hardware Initialization */
	Joystick_Init();
	LEDs_Init();
	Buttons_Init();
	USB_Init();
}

/** Event handler for the USB_Connect event. This indicates that the device is enumerating via the status LEDs and
*  starts the library USB task to begin the enumeration and USB management process.
*/
void EVENT_USB_Device_Connect(void)
{
	/* Indicate USB enumerating */
	LEDs_SetAllLEDs(LEDMASK_USB_ENUMERATING);
}

/** Event handler for the USB_Disconnect event. This indicates that the device is no longer connected to a host via
*  the status LEDs and stops the USB management and joystick reporting tasks.
*/
void EVENT_USB_Device_Disconnect(void)
{
	/* Indicate USB not ready */
	LEDs_SetAllLEDs(LEDMASK_USB_NOTREADY);
}

/** Event handler for the USB_ConfigurationChanged event. This is fired when the host set the current configuration
*  of the USB device after enumeration - the device endpoints are configured and the joystick reporting task started.
*/
void EVENT_USB_Device_ConfigurationChanged(void)
{
	bool ConfigSuccess = true;

	/* Setup HID Report Endpoint */
	ConfigSuccess &= Endpoint_ConfigureEndpoint(JOYSTICK_EPADDR, EP_TYPE_INTERRUPT, JOYSTICK_EPSIZE, 1);
	ConfigSuccess &= Endpoint_ConfigureEndpoint(JOYSTICK_OUT_EPADDR, EP_TYPE_INTERRUPT, JOYSTICK_OUT_EPSIZE, 1);

	/* Indicate endpoint configuration success or failure */
	LEDs_SetAllLEDs(ConfigSuccess ? LEDMASK_USB_READY : LEDMASK_USB_ERROR);
}

/** Event handler for the USB_ControlRequest event. This is used to catch and process control requests sent to
*  the device from the USB host before passing along unhandled control requests to the library for processing
*  internally.
*/
void EVENT_USB_Device_ControlRequest(void)
{
	/* Handle HID Class specific requests */
	switch (USB_ControlRequest.bRequest)
	{
		case HID_REQ_GetReport:
		if (USB_ControlRequest.bmRequestType == (REQDIR_DEVICETOHOST | REQTYPE_CLASS | REQREC_INTERFACE))
		{
			USB_JoystickReport_Data_t JoystickReportData;

			/* Create the next HID report to send to the host */
			GetNextReport(&JoystickReportData);

			Endpoint_ClearSETUP();

			/* Write the report data to the control endpoint */
			Endpoint_Write_Control_Stream_LE(&JoystickReportData, sizeof(JoystickReportData));
			Endpoint_ClearOUT();
		}

		break;
		case HID_REQ_SetReport:
		if (USB_ControlRequest.bmRequestType == (REQDIR_HOSTTODEVICE | REQTYPE_CLASS | REQREC_INTERFACE))
		{
			Endpoint_ClearSETUP();

			/* Wait until the LED report has been sent by the host */
			while (!(Endpoint_IsOUTReceived()))
			{
				if (USB_DeviceState == DEVICE_STATE_Unattached)
				return;
			}

			/* Read in the LED report from the host */
				//Endpoint_Discard_8();
				uint8_t getOptions = Endpoint_Read_8();
				int16_t getForce = Endpoint_Read_16_LE();

			Endpoint_ClearOUT();
			Endpoint_ClearStatusStage();

			/* Process the incoming LED report */
				processStickOut(getOptions,getForce);
		}

		break;
	}
}


/** Fills the given HID report data structure with the next HID report to send to the host.
*
*  \param[out] ReportData  Pointer to a HID report data structure to be filled
*
*  \return Boolean \c true if the new report differs from the last report, \c false otherwise
*/
bool GetNextReport(USB_JoystickReport_Data_t* const ReportData)
{
	/* Clear the report contents */
	memset(ReportData, 0, sizeof(USB_JoystickReport_Data_t));
	static AxisStore Axis;
	ReadStick(&Axis);
	ReportData->X = Axis.X;
	ReportData->Y = Axis.Y;

	//// get buttons and Hat
	uint32_t Buffer = ReadGrip();
	FccSettings(Buffer);

	// if config mode was set, supress button output
	if (gIsConfig) {
		ReportData->Buttons = 0;
		ReportData->Hat = 8;
		} else {
		ReportData->Buttons = Buffer;
		ReportData->Hat = Buffer >> 20;
	}

	ReportData->CurrentOptions = gOptions;
	ReportData->CurrentUserDef=gUserDefinedForce;
	/* Return whether the new report is different to the previous report or not */
	return true;
}


/** Function to manage HID report generation and transmission to the host. */
void HID_Task(void)
{
	/* Device must be connected and configured for the task to run */
	if (USB_DeviceState != DEVICE_STATE_Configured)
	return;

	/* Select the Joystick Report Endpoint */
	Endpoint_SelectEndpoint(JOYSTICK_EPADDR);

	/* Check to see if the host is ready for another packet */
	if (Endpoint_IsINReady())
	{
		USB_JoystickReport_Data_t JoystickReportData;

		/* Create the next HID report to send to the host */
		GetNextReport(&JoystickReportData);

		/* Write Joystick Report Data */
		Endpoint_Write_Stream_LE(&JoystickReportData, sizeof(JoystickReportData), NULL);
//
				//// Send Config options
				//Endpoint_Write_8(gOptions);
				//Endpoint_Write_16_LE(gUserDefinedForce);
		/* Finalize the stream transfer to send the last packet */
		Endpoint_ClearIN();

		/* Clear the report data afterwards */
		memset(&JoystickReportData, 0, sizeof(JoystickReportData));
	}

	// Now read config options from the system, if any.
	/* Select the Joystick Report Endpoint */
	Endpoint_SelectEndpoint(JOYSTICK_OUT_EPADDR);

	if (Endpoint_IsOUTReceived()) {
		/* Check to see if the packet contains data */

			/* Read in the LED report from the host */
			//Endpoint_Discard_8();
			uint8_t getOptions = Endpoint_Read_8();
			int16_t getForce = Endpoint_Read_16_LE();

			Endpoint_ClearOUT();

			/* Process the incoming LED report */
			processStickOut(getOptions,getForce);
	}


}
