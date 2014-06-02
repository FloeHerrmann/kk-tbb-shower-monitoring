/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */
/* Author:    m2m systems GmbH, Florian Herrmann                             */
/* Copyright: 2014, m2m systems GmbH, Florian Herrmann                       */
/* Purpose:                                                                  */
/*   Monitoring and measuring the water consuption and temperatures of a     */
/*   shower to calculate the costs for taking this shower.                   */
/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <EEPROM.h>
#include <SPI.h>
#include <GD2.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include "interface_assets.h"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Define own data types
#define ubyte uint8_t
#define uint uint16_t
#define ulong uint32_t

// Number of samples that will be displayed in the charts
#define SAMPLES 20

// Milliseconds between two samples
#define SAMPLES_INTERVALL 1000

// Interrupt number 5 = Arduino pin 18
#define FLOW_SENSOR_PIN 5

// Divider to get the water amout from the flow sensor impulses
#define FLOW_SENSOR_DIVIDER 100

// Pint that is used for the communication with the temperature sensors
#define ONE_WIRE_BUS_PIN 14

// The flow sensor impulses per litre
#define IMPULSES_PER_LITER 180.0

// Defines when the reset button will be shown [milliseconds]
#define RESET_BUTTON_TIMEOUT 30000

// Defines when all values will be cleared [milliseconds]
#define VALUES_CLEAR_TIMEOUT 300000

// Distance between display border and the interface elements
#define MARGIN_TOP 10
#define MARGIN_RIGHT 10
#define MARGIN_BOTTOM 10
#define MARGIN_LEFT 10

// Resolution of the display 480x272
#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 272

// Definition of the current that is currently displayed
#define SCREEN_MAIN 0
#define SCREEN_SETTINGS 1

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Array for storing the flow sensor samples
float WaterFlowValues[SAMPLES];

// Array for storing the temperature samples
float TemperatureValues[SAMPLES];

// Is true if the "Reset" button is pressed, otherwise false
boolean ResetButtonIsPressed = false;

// Is true if the "Settings" icon is pressed, otherwise false
boolean SettingsButtonIsPressed = false;

// Is true if the "Back" icon is pressed, otherwise false
boolean BackButtonIsPressed = false;

// Is true if the "Save" button is pressed, otherwise false
boolean SaveButtonIsPressed = false;

// Is true if the shower is running, which means some water flows
boolean ShowerIsRunning = false;

// If true the "Reset" button will be shown
boolean ResetIsShown = false;

// Helper variable for taking samples
ulong SampleTimeHelper = 0;

// Helper variable for the timestamp of the last sample
ulong ImpulsesTimeHelper = -1;

// Helper variable for the number of taken samples
uint NumberOfSamplesHelper = 0;

// Helper variable for the x-axis difference between two values
uint XAxisFactor = 0;

// Helper array for the addresses of our temperature sensors
DeviceAddress WarmWaterSensorAddress = { 0x28, 0x7A , 0x61 , 0x5D , 0x05 , 0x00 , 0x00 , 0xE5 };
DeviceAddress ColdWaterSensorAddress = { 0x28, 0xFE , 0x02 , 0x5D , 0x05 , 0x00 , 0x00 , 0x8C };

// Counter for the impulses of the flow sensor during the last sample interval
volatile ulong FlowSensorPulses = 0;

// Counter the total amount of flow sensor impulses
volatile ulong TotalFlowSensorPulses = 0;

// Setup a OneWire instance to communicate with OneWire devices
OneWire OneWireBus( ONE_WIRE_BUS_PIN );

// Setup a DallesTemperature instance to get temperature from our sensors
DallasTemperature TemperatureSensors( &OneWireBus );

// Current temperature of the warm water
float CurrentWarmWaterTemperature;

// Current temperature of the cold water
float CurrentColdWaterTemperature;

// Current total costs
float CurrentCosts = 0.0;

// Current total water amount
float CurrentWater = 0.0;

// Current screen that is displayed
ubyte CurrentScreen;

bool TouchTagsHelper[30];

float SettingsWaterCosts = 0.0;
float SettingsEnergyCosts = 0.0;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void setup() {
	Serial.begin( 57600 );

	// Initialize value arrays
	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) {
		WaterFlowValues[ index ] = -1.0;
		TemperatureValues[ index ] = -1.0;
	}

	for( uint16_t index = 0 ; index < 30 ; index++ ) {
		TouchTagsHelper[ index ] = false;
	}

	// Start the communication with our temperature sensors
	TemperatureSensors.begin();
	TemperatureSensors.setResolution( WarmWaterSensorAddress , 9 );
	TemperatureSensors.setResolution( ColdWaterSensorAddress , 9 );
	TemperatureSensors.requestTemperatures();
	CurrentWarmWaterTemperature = TemperatureSensors.getTempC( WarmWaterSensorAddress );
	CurrentColdWaterTemperature = TemperatureSensors.getTempC( ColdWaterSensorAddress );

	// Initialize display and load assets
	GD.begin();
	LoadImages();

	EEPROMReadFloat( &SettingsWaterCosts , 4000 );
	EEPROMReadFloat( &SettingsEnergyCosts , 4020 );

	// Initialize variables
	SampleTimeHelper = millis();
	XAxisFactor = (int)( 222.0 / (float)SAMPLES );
	CurrentScreen = SCREEN_SETTINGS;

	// Attach an interrupt for the flow sensor
	attachInterrupt( FLOW_SENSOR_PIN , CountImpulses , FALLING) ;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void loop () {

	if( FlowSensorPulses != 0 && ShowerIsRunning == false ) {
		ImpulsesTimeHelper = millis();
		ShowerIsRunning = true;
		ResetIsShown = false;
	} else if( FlowSensorPulses != 0 ) {
		ImpulsesTimeHelper = millis();
	} else if( millis() - ImpulsesTimeHelper > RESET_BUTTON_TIMEOUT && FlowSensorPulses == 0 && ShowerIsRunning == true ){
		ResetIsShown = true;
		ShowerIsRunning = false;
	} else if( millis() - ImpulsesTimeHelper > VALUES_CLEAR_TIMEOUT && FlowSensorPulses == 0 && ResetIsShown == true ) {
		ResetIsShown = false;
		ResetValues();
	}


	// Get the input of the touch display and check if something relevant is pressed
	GD.get_inputs();
	if( GD.inputs.tag == 1 ) {
		ResetButtonIsPressed = true;
	} else if( GD.inputs.tag == 2 ) {
		SettingsButtonIsPressed = true;
	} else if( GD.inputs.tag == 3 ) {
		BackButtonIsPressed = true;
	} else if( GD.inputs.tag == 4 ) {
		SaveButtonIsPressed = true;
	} else if( GD.inputs.tag >= 10 && GD.inputs.tag <= 30 ) {
		TouchTagsHelper[ GD.inputs.tag ] = true;
	} else {
		if( ResetButtonIsPressed == true ) {
			delay( 50 );
			ResetButtonIsPressed = false;
			ResetIsShown = false;
			ResetValues();
		} else if( SettingsButtonIsPressed == true ) {
			delay( 50 );
			SettingsButtonIsPressed = false;
			CurrentScreen = SCREEN_SETTINGS;
		} else if( BackButtonIsPressed == true ) {
			delay( 50 );
			EEPROMReadFloat( &SettingsWaterCosts , 4000 );
			EEPROMReadFloat( &SettingsEnergyCosts , 4020 );
			BackButtonIsPressed = false;
			CurrentScreen = SCREEN_MAIN;
		} else if( SaveButtonIsPressed == true ) {
			delay( 50 );
			SaveButtonIsPressed = false;
			EEPROMWriteFloat( &SettingsWaterCosts , 4000 );
			EEPROMWriteFloat( &SettingsEnergyCosts , 4020 );
			CurrentScreen = SCREEN_MAIN;
		} else {
			if( TouchTagsHelper[10] == true ) {
				if( SettingsWaterCosts > 0 ) SettingsWaterCosts -= 100.0;
				TouchTagsHelper[10] = false;
			}
			if( TouchTagsHelper[11] == true ) {
				if( SettingsWaterCosts < 900 ) SettingsWaterCosts += 100.0;
				TouchTagsHelper[11] = false;
			}
			if( TouchTagsHelper[12] == true ) {
				if( SettingsWaterCosts > 0 ) SettingsWaterCosts -= 10.0;
				TouchTagsHelper[12] = false;
			}
			if( TouchTagsHelper[13] == true ) {
				if( SettingsWaterCosts < 990 ) SettingsWaterCosts += 10.0;
				TouchTagsHelper[13] = false;
			}
			if( TouchTagsHelper[14] == true ) {
				if( SettingsWaterCosts > 0 ) SettingsWaterCosts -= 1.0;
				TouchTagsHelper[14] = false;
			}
			if( TouchTagsHelper[15] == true ) {
				if( SettingsWaterCosts < 999 )SettingsWaterCosts += 1.0;
				TouchTagsHelper[15] = false;
			}
			if( TouchTagsHelper[16] == true ) {
				if( SettingsWaterCosts > 0 ) SettingsWaterCosts -= 0.1;
				TouchTagsHelper[16] = false;
			}
			if( TouchTagsHelper[17] == true ) {
				if( SettingsWaterCosts < 999.9 )SettingsWaterCosts += 0.1;
				TouchTagsHelper[17] = false;
			}
			if( TouchTagsHelper[18] == true ) {
				if( SettingsWaterCosts > 0 ) SettingsWaterCosts -= 0.01;
				TouchTagsHelper[18] = false;
			}
			if( TouchTagsHelper[19] == true ) {
				if( SettingsWaterCosts < 999.99 )SettingsWaterCosts += 0.01;
				TouchTagsHelper[19] = false;
			}
			if( TouchTagsHelper[20] == true ) {
				if( SettingsEnergyCosts > 0 ) SettingsEnergyCosts -= 100.0;
				TouchTagsHelper[20] = false;
			}
			if( TouchTagsHelper[21] == true ) {
				if( SettingsEnergyCosts < 900.0 ) SettingsEnergyCosts += 100.0;
				TouchTagsHelper[21] = false;
			}
			if( TouchTagsHelper[22] == true ) {
				if( SettingsEnergyCosts > 0 ) SettingsEnergyCosts -= 10.0;
				TouchTagsHelper[22] = false;
			}
			if( TouchTagsHelper[23] == true ) {
				if( SettingsEnergyCosts < 990.0 ) SettingsEnergyCosts += 10.0;
				TouchTagsHelper[23] = false;
			}
			if( TouchTagsHelper[24] == true ) {
				if( SettingsEnergyCosts > 0 ) SettingsEnergyCosts -= 1.0;
				TouchTagsHelper[24] = false;
			}
			if( TouchTagsHelper[25] == true ) {
				if( SettingsEnergyCosts < 999.0 ) SettingsEnergyCosts += 1.0;
				TouchTagsHelper[25] = false;
			}
			if( TouchTagsHelper[26] == true ) {
				if( SettingsEnergyCosts > 0 ) SettingsEnergyCosts -= 0.1;
				TouchTagsHelper[26] = false;
			}
			if( TouchTagsHelper[27] == true ) {
				if( SettingsEnergyCosts < 999.9 ) SettingsEnergyCosts += 0.1;
				TouchTagsHelper[27] = false;
			}
			if( TouchTagsHelper[28] == true ) {
				if( SettingsEnergyCosts > 0 ) SettingsEnergyCosts -= 0.01;
				TouchTagsHelper[28] = false;
			}
			if( TouchTagsHelper[29] == true ) {
				if( SettingsEnergyCosts < 999.99 ) SettingsEnergyCosts += 0.01;
				TouchTagsHelper[29] = false;
			}

		}
	}

	if( ShowerIsRunning == true ) {

		if( millis() - SampleTimeHelper >= SAMPLES_INTERVALL ) {
			float pulsesF = ((float)FlowSensorPulses) / ((float)FLOW_SENSOR_DIVIDER);
			TemperatureSensors.requestTemperatures();
			CurrentWarmWaterTemperature = TemperatureSensors.getTempC( WarmWaterSensorAddress );
			if( NumberOfSamplesHelper < SAMPLES ) {
				WaterFlowValues[ NumberOfSamplesHelper ] = pulsesF;
				TemperatureValues[ NumberOfSamplesHelper ] = CurrentWarmWaterTemperature;
				NumberOfSamplesHelper++;
			} else {
				for( uint index = 0 ; index < (SAMPLES-1) ; index++ ) {
					WaterFlowValues[ index ] = WaterFlowValues[ index + 1 ];
					TemperatureValues[ index ] = TemperatureValues[ index + 1 ];
				}
				WaterFlowValues[ (SAMPLES-1) ] = pulsesF;
				TemperatureValues[ (SAMPLES-1) ] = CurrentWarmWaterTemperature;
			}
			TotalFlowSensorPulses += FlowSensorPulses;
			FlowSensorPulses = 0;
			SampleTimeHelper = millis();
		}
	} else {
		if( millis() - SampleTimeHelper >= SAMPLES_INTERVALL ) {
			TemperatureSensors.requestTemperatures();
			CurrentWarmWaterTemperature = TemperatureSensors.getTempC( WarmWaterSensorAddress );
		}
	}

	CurrentWater = (float)TotalFlowSensorPulses / IMPULSES_PER_LITER;
	CurrentCosts = CurrentWater * 0.04;

	if( CurrentScreen == SCREEN_MAIN ) {
		DrawMainScreen();
		DrawResetButton();
		DrawTouchTags();
		DrawCharts();
		DrawCosts( CurrentCosts );
		DrawTemperature( CurrentWarmWaterTemperature );
		DrawWater( CurrentWater );
	} else if( CurrentScreen == SCREEN_SETTINGS ) {
		DrawSettingscreen();
		DrawSaveButton();
		DrawWaterCosts( SettingsWaterCosts );
		DrawEnergyCosts( SettingsEnergyCosts );
		DrawTouchTags();
	}
	GD.swap();

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CountImpulses( ){
	FlowSensorPulses = FlowSensorPulses + 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ResetValues(){
	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) {
		WaterFlowValues[ index ] = -1.0;
		TemperatureValues[ index ] = -1.0;
	}
	FlowSensorPulses = 0;
	TotalFlowSensorPulses = 0;
	NumberOfSamplesHelper = 0;
	CurrentCosts = 0.0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void LoadImages(){
	LOAD_ASSETS();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DrawCharts(){

	float OffsetFactor = 96.0 / 4.0;
	uint xStartCordinate = 335;
	uint yStartCordinate = 11;

	GD.ColorRGB( 151 , 187 , 205 );
	GD.ColorA( 255 );
	GD.LineWidth( 1 * 10 );
  	GD.Begin( LINE_STRIP );

  	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) {
  		if( WaterFlowValues[index] != -1.0 ) {
			float xOffsetF = WaterFlowValues[ index ] * OffsetFactor;
			int xOffset = (int)xOffsetF;
			int yOffset = index * XAxisFactor;
			GD.Vertex2ii( xStartCordinate + xOffset , yStartCordinate + yOffset );
		}
	}

	GD.ColorRGB( 151 , 187 , 205 );
	GD.ColorA( 255 );
	GD.PointSize( 16 * 2 );
	GD.Begin( POINTS );

	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) {
		if( WaterFlowValues[ index ] != -1.0 ) {
			float xOffsetF = WaterFlowValues[ index ] * OffsetFactor;
			int xOffset = (int)xOffsetF;
			int yOffset = index * XAxisFactor;
			GD.Vertex2ii( xStartCordinate + xOffset , yStartCordinate + yOffset );
		}
	}

	OffsetFactor = 96.0 / 60.0;
	xStartCordinate = 182;
	yStartCordinate = 11;

	GD.ColorRGB( 208 , 2 , 27 );
	GD.ColorA( 255 );
	GD.LineWidth( 1 * 10 );
  	GD.Begin( LINE_STRIP );

  	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) {
  		if( TemperatureValues[index] != -1.0 ) {
			float xOffsetF = TemperatureValues[ index ] * OffsetFactor;
			int xOffset = (int)xOffsetF;
			int yOffset = index * XAxisFactor;
			GD.Vertex2ii( xStartCordinate + xOffset , yStartCordinate + yOffset );
		}
	}

	GD.ColorRGB( 208 , 2 , 27 );
	GD.ColorA( 255 );
	GD.PointSize( 16 * 2 );
	GD.Begin( POINTS );

	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) {
		if( TemperatureValues[ index ] != -1.0 ) {
			float xOffsetF = TemperatureValues[ index ] * OffsetFactor;
			int xOffset = (int)xOffsetF;
			int yOffset = index * XAxisFactor;
			GD.Vertex2ii( xStartCordinate + xOffset , yStartCordinate + yOffset );
		}
	}
/*
	OffsetFactor = 96.0 / 4.0;
	xStartCordinate = 336;
	yStartCordinate = 11;

	for( uint16_t index = 0 ; index < (SAMPLES-1) ; index++ ) {
		if( WaterFlowValues[ index + 1 ] != -1.0 ) {
			GD.ColorRGB( 151 , 187 , 205 );
			GD.ColorA( 80 );
			Poly waterFlow;
			waterFlow.begin();
			int xOffset = (int)( WaterFlowValues[ index ] * OffsetFactor );
			int xCordinate = ( 16 * xStartCordinate ) + 16 * xOffset;
			int yCordinate = 1 + ( 16 * yStartCordinate ) + 16 * XAxisFactor * index;
			waterFlow.v( 16 * xStartCordinate , yCordinate );
			waterFlow.v( xCordinate , yCordinate );
			xOffset = (int)( WaterFlowValues[ index + 1 ] * OffsetFactor );
			xCordinate = ( 16 * xStartCordinate ) + 16 * xOffset;
			yCordinate = 1 + ( 16 * yStartCordinate ) + 16 * XAxisFactor * ( index + 1 );
			waterFlow.v( xCordinate , yCordinate );
			waterFlow.v( 16 * xStartCordinate , yCordinate );
			waterFlow.draw();
		}
	}

	OffsetFactor = 96.0 / 60.0;
	xStartCordinate = 183;
	yStartCordinate = 11;

	for( uint16_t index = 0 ; index < (SAMPLES-1) ; index++ ) {
		if( TemperatureValues[ index + 1 ] != -1.0 ) {
			GD.ColorRGB( 208 , 2 , 27 );
			GD.ColorA( 60 );
			Poly waterFlow;
			waterFlow.begin();
			int xOffset = (int)( TemperatureValues[ index ] * OffsetFactor );
			int xCordinate = ( 16 * xStartCordinate ) + 16 * xOffset;
			int yCordinate = 1 + ( 16 * yStartCordinate ) + 16 * XAxisFactor * index;
			waterFlow.v( 16 * xStartCordinate , yCordinate );
			waterFlow.v( xCordinate , yCordinate );
			xOffset = (int)( TemperatureValues[ index + 1 ] * OffsetFactor );
			xCordinate = ( 16 * xStartCordinate ) + 16 * xOffset;
			yCordinate = 1 + ( 16 * yStartCordinate ) + 16 * XAxisFactor * ( index + 1 );
			waterFlow.v( xCordinate , yCordinate );
			waterFlow.v( 16 * xStartCordinate , yCordinate );
			waterFlow.draw();
		}
	}
*/
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DrawMainScreen(){

	GD.ClearColorRGB( 0xFFFFFF );
	GD.Clear();
	GD.ColorRGB( 0xFFFFFF );
	GD.Begin(BITMAPS);

	// Water Icon & Text
	GD.Vertex2ii( DISPLAY_WIDTH - MARGIN_TOP - 16 , MARGIN_LEFT - 3 , ICONS_HANDLE , 4 );
	GD.Vertex2ii( DISPLAY_WIDTH - MARGIN_TOP - 29 , MARGIN_LEFT + 12 , INTERFACE_HANDLE , 0 );

	// Temperature Icon & Text
	GD.Vertex2ii( DISPLAY_WIDTH - MARGIN_TOP - 16 - 154 , MARGIN_LEFT , ICONS_HANDLE , 3 );
	GD.Vertex2ii( DISPLAY_WIDTH - MARGIN_TOP - 29 - 154 , MARGIN_LEFT + 19 , INTERFACE_HANDLE , 1 );

	// Temperature Icon & Text
	GD.Vertex2ii( DISPLAY_WIDTH - MARGIN_TOP - 16 - 308 , MARGIN_LEFT - 1 , ICONS_HANDLE , 2 );
	GD.Vertex2ii( DISPLAY_WIDTH - MARGIN_TOP - 28 - 308 , MARGIN_LEFT + 17 , INTERFACE_HANDLE , 2 );

	// Water Chart Axis
	GD.Vertex2ii( DISPLAY_WIDTH - 144 , DISPLAY_HEIGHT - MARGIN_RIGHT -24 , AXES_HANDLE , 0 );

	// Temperature Chart Axis
	GD.Vertex2ii( DISPLAY_WIDTH - MARGIN_TOP - 289 , DISPLAY_HEIGHT - MARGIN_RIGHT -23 , AXES_HANDLE , 1 );

	// Settings Icon
	GD.Vertex2ii( MARGIN_BOTTOM + 7 , DISPLAY_HEIGHT - MARGIN_RIGHT - 29 , ICONS_HANDLE , 0 );

	// Water Chart
	DrawChartLines( DISPLAY_WIDTH - 140 , MARGIN_LEFT );
	
	// Temperature Chart
	DrawChartLines( DISPLAY_WIDTH - MARGIN_TOP - 286 , MARGIN_LEFT );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DrawSettingscreen(){

	GD.ClearColorRGB( 0xFFFFFF );
	GD.Clear();
	GD.ColorRGB( 0xFFFFFF );
	GD.Begin(BITMAPS);

	// Water Icon & Text
	GD.Vertex2ii( DISPLAY_WIDTH - MARGIN_TOP - 16 , MARGIN_LEFT - 3 , ICONS_HANDLE , 4 );
	GD.Vertex2ii( DISPLAY_WIDTH - MARGIN_TOP - 29 , MARGIN_LEFT + 12 , INTERFACE_HANDLE , 3 );

	// Temperature Icon & Text
	GD.Vertex2ii( DISPLAY_WIDTH - MARGIN_TOP - 16 - 214 , MARGIN_LEFT , ICONS_HANDLE , 5 );
	GD.Vertex2ii( DISPLAY_WIDTH - MARGIN_TOP - 29 - 214 , MARGIN_LEFT + 19 , INTERFACE_HANDLE , 4 );

	// Back Icon
	GD.Vertex2ii( MARGIN_BOTTOM + 7 , MARGIN_LEFT + 12 , ICONS_HANDLE , 1 );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DrawChartLines( uint x , uint y ) {
	GD.Begin( LINES );
	GD.LineWidth( 10 );

	GD.ColorRGB( 0xD8D8D8 );
	int width = 3;
	int offset = 3;
	float number = 222 / (width+offset);

	for( uint i = 1 ; i < 7 ; i++ ) {
		for( uint j = 0 ; j < number ; j++ ) {
			GD.Vertex2ii( x + i*16 , y + j*(width+offset) );
  			GD.Vertex2ii( x + i*16 , y + j*(width+offset)+width );
  		}
	}

	GD.ColorRGB( 0x979797 );
	GD.Vertex2ii( x , y );
	GD.Vertex2ii( x , y + 222 );

	GD.LineWidth( 12 );
	GD.Vertex2ii( x , y );
	GD.Vertex2ii( x + 102 , y );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DrawTouchTags(){
	GD.ColorRGB( 0xFFFFFF );
	GD.ColorA( 0 );
	GD.Begin( RECTS );
	if( CurrentScreen == SCREEN_MAIN ) {
		if( ResetIsShown == true ) {
			GD.Tag( 1 );
			GD.Vertex2ii( 7 , 47 );
			GD.Vertex2ii( 47 , 223 );
		}
		GD.Tag( 2 );
		GD.Vertex2ii( 11 , 232 );
		GD.Vertex2ii( 41 , 262 );
	} else if( CurrentScreen == SCREEN_SETTINGS ) {
		GD.Tag( 3 );
		GD.Vertex2ii( MARGIN_BOTTOM + 1 , MARGIN_LEFT + 5 );
		GD.Vertex2ii( MARGIN_BOTTOM + 31 , MARGIN_LEFT + 35 );
		GD.Tag( 4 );
		GD.Vertex2ii( 7 , 47 );
		GD.Vertex2ii( 47 , 223 );
	}
	GD.ColorRGB( 0xFFFFFF );
	GD.ColorA( 255 );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DrawResetButton(){
	GD.ColorRGB( 0xFFFFFF );
	GD.Begin(BITMAPS);
	GD.Vertex2ii( 10 , 60 , INTERFACE_HANDLE , 5 );
	if( ResetIsShown == false ) {
		GD.ColorRGB( 0xFFFFFF );
		GD.ColorA( 240 );
		GD.Begin( RECTS );
		GD.Vertex2ii( 7 , 47 );
		GD.Vertex2ii( 47 , 223 );
	} 
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DrawSaveButton(){
	GD.ColorRGB( 0xFFFFFF );
	GD.Begin(BITMAPS);
	GD.Vertex2ii( 10 , 60 , INTERFACE_HANDLE , 6 );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DrawCosts( float value ){
	GD.ColorRGB( 0xFFFFFF );
	GD.Begin( BITMAPS );
	
	ubyte hundret = value / 100; value -= (hundret*100);
	ubyte ten = value / 10; value -= (ten*10);
	ubyte one = value / 1; value -= (one*1);
	ubyte tenth = value / 0.1; value -= (tenth*0.1);
	ubyte hundreth = value / 0.01;

	uint width = 150;
	if( hundret != 0 ) width += 32;
	if( ten != 0 ) width += 32;

	uint y = ( 272 - width ) / 2;
	uint x = 75;

	if( hundret != 0 ) {
		GD.Vertex2ii( x , y , BIG_NUMBERS_HANDLE , hundret );
		y += 32;
	}
	
	if( ten != 0 || hundret != 0 ) {
		GD.Vertex2ii( x , y , BIG_NUMBERS_HANDLE , ten );
		y += 32;
	}
	
	GD.Vertex2ii( x , y , BIG_NUMBERS_HANDLE , one ); y += 32;

	GD.Vertex2ii( x - 15 , y , BIG_SIGNS_HANDLE , 0 ); y += 16;

	GD.Vertex2ii( x , y , BIG_NUMBERS_HANDLE , tenth ); y += 32;

	GD.Vertex2ii( x , y , BIG_NUMBERS_HANDLE , hundreth ); y += 32;

	GD.Vertex2ii( x , y , BIG_SIGNS_HANDLE , 1 );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DrawTemperature( float value ){
	GD.ColorRGB( 0xFFFFFF );
	GD.Begin( BITMAPS );
	
	ubyte hundret = value / 100; value -= (hundret*100);
	ubyte ten = value / 10; value -= (ten*10);
	ubyte one = value / 1; value -= (one*1);
	ubyte tenth = value / 0.1; value -= (tenth*0.1);
	ubyte hundreth = value / 0.01;

	uint width = 74;
	if( hundret != 0 ) width += 13;
	if( ten != 0 ) width += 13;

	uint y = 272 - width - 5;
	uint x = 298;

	if( hundret != 0 ) {
		GD.Vertex2ii( x , y , SMALL_NUMBERS_HANDLE , hundret );
		y += 13;
	}
	
	if( ten != 0 || hundret != 0 ) {
		GD.Vertex2ii( x , y , SMALL_NUMBERS_HANDLE , ten );
		y += 13;
	}
	
	GD.Vertex2ii( x , y , SMALL_NUMBERS_HANDLE , one ); y += 13;

	GD.Vertex2ii( x - 4 , y , SMALL_SIGNS_HANDLE , 0 ); y += 8;

	GD.Vertex2ii( x , y , SMALL_NUMBERS_HANDLE , tenth ); y += 13;

	GD.Vertex2ii( x , y , SMALL_NUMBERS_HANDLE , hundreth ); y += 13;

	GD.Vertex2ii( x , y , SMALL_SIGNS_HANDLE , 1 ); y += 10;
	GD.Vertex2ii( x - 2 , y , SMALL_SIGNS_HANDLE , 2 );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DrawWater( float value ) {
	GD.ColorRGB( 0xFFFFFF );
	GD.Begin( BITMAPS );
	
	ubyte hundret = value / 100; value -= (hundret*100);
	ubyte ten = value / 10; value -= (ten*10);
	ubyte one = value / 1; value -= (one*1);
	ubyte tenth = value / 0.1; value -= (tenth*0.1);
	ubyte hundreth = value / 0.01;

	uint width = 63;
	if( hundret != 0 ) width += 13;
	if( ten != 0 ) width += 13;

	uint y = 272 - width - 5;
	uint x = 451;

	if( hundret != 0 ) {
		GD.Vertex2ii( x , y , SMALL_NUMBERS_HANDLE , hundret );
		y += 13;
	}
	
	if( ten != 0 || hundret != 0 ) {
		GD.Vertex2ii( x , y , SMALL_NUMBERS_HANDLE , ten );
		y += 13;
	}
	
	GD.Vertex2ii( x , y , SMALL_NUMBERS_HANDLE , one ); y += 13;

	GD.Vertex2ii( x - 4 , y , SMALL_SIGNS_HANDLE , 0 ); y += 8;

	GD.Vertex2ii( x , y , SMALL_NUMBERS_HANDLE , tenth ); y += 13;

	GD.Vertex2ii( x , y , SMALL_NUMBERS_HANDLE , hundreth ); y += 13;

	GD.Vertex2ii( x , y , SMALL_SIGNS_HANDLE , 4 );
}

void DrawWaterCosts( float value ) {
	GD.ColorRGB( 0xFFFFFF );
	GD.Begin( BITMAPS );
	
	ubyte hundret = value / 100; value -= (hundret*100);
	ubyte ten = value / 10; value -= (ten*10);
	ubyte one = value / 1; value -= (one*1);
	ubyte tenth = value / 0.1; value -= (tenth*0.1);
	ubyte hundreth = value / 0.01;

	uint width = 150;
	if( hundret != 0 ) width += 32;
	if( ten != 0 ) width += 32;

	uint y = ( 272 - width ) / 2;
	uint x = 335;

	if( hundret != 0 ) {
		GD.Vertex2ii( x , y , BIG_NUMBERS_HANDLE , hundret );
		DrawUpAndDownArrows( x - 25 , y , 10 , 11 );
		y += 32;
	}
	
	if( ten != 0 || hundret != 0 ) {
		GD.Vertex2ii( x , y , BIG_NUMBERS_HANDLE , ten );
		// Up & Down Icons
		DrawUpAndDownArrows( x - 25 , y , 12 , 13 );
		y += 32;
	}
	
	GD.Vertex2ii( x , y , BIG_NUMBERS_HANDLE , one );
	DrawUpAndDownArrows( x - 25 , y , 14 , 15 );
	y += 32;

	GD.Vertex2ii( x - 15 , y , BIG_SIGNS_HANDLE , 0 ); y += 16;

	GD.Vertex2ii( x , y , BIG_NUMBERS_HANDLE , tenth );
	DrawUpAndDownArrows( x - 25 , y , 16 , 17 );
	y += 32;

	GD.Vertex2ii( x , y , BIG_NUMBERS_HANDLE , hundreth );
	DrawUpAndDownArrows( x - 25 , y , 18 , 19 );
	y += 32;

	GD.Vertex2ii( x , y + 2 , BIG_SIGNS_HANDLE , 2 );
}

void DrawEnergyCosts( float value ) {
	GD.ColorRGB( 0xFFFFFF );
	GD.Begin( BITMAPS );
	
	ubyte hundret = value / 100; value -= (hundret*100);
	ubyte ten = value / 10; value -= (ten*10);
	ubyte one = value / 1; value -= (one*1);
	ubyte tenth = value / 0.1; value -= (tenth*0.1);
	ubyte hundreth = value / 0.01;

	uint width = 150;
	if( hundret != 0 ) width += 32;
	if( ten != 0 ) width += 32;

	uint y = ( 272 - width ) / 2;
	uint x = 125;

	if( hundret != 0 ) {
		GD.Vertex2ii( x , y , BIG_NUMBERS_HANDLE , hundret );
		DrawUpAndDownArrows( x - 25 , y , 20 , 21 );
		y += 32;
	}
	
	if( ten != 0 || hundret != 0 ) {
		GD.Vertex2ii( x , y , BIG_NUMBERS_HANDLE , ten );
		DrawUpAndDownArrows( x - 25 , y , 22 , 23 );
		y += 32;
	}
	
	GD.Vertex2ii( x , y , BIG_NUMBERS_HANDLE , one );
	DrawUpAndDownArrows( x - 25 , y , 24 , 25 );
	y += 32;

	GD.Vertex2ii( x - 15 , y , BIG_SIGNS_HANDLE , 0 ); y += 16;

	GD.Vertex2ii( x , y , BIG_NUMBERS_HANDLE , tenth );
	DrawUpAndDownArrows( x - 25 , y , 26 , 27 );
	y += 32;

	GD.Vertex2ii( x , y , BIG_NUMBERS_HANDLE , hundreth );
	DrawUpAndDownArrows( x - 25 , y , 28 , 29 );
	y += 32;

	GD.Vertex2ii( x , y + 2 , BIG_SIGNS_HANDLE , 2 );
}

void DrawUpAndDownArrows( uint x , uint y , uint tag01 , uint tag02 ) {
	// Up & Down Icons
		GD.Vertex2ii( x + 6 , y + 7 , ICONS_HANDLE , 6 );
		GD.Vertex2ii( x + 77 , y + 7 , ICONS_HANDLE , 7 );

		GD.ColorRGB( 0xFFFFFF );
		GD.ColorA( 0 );
		GD.Begin( RECTS );

		GD.Tag( tag01 );
		GD.Vertex2ii( x , y + 6 );
		GD.Vertex2ii( x + 28 , y + 24 );
		
		GD.Tag( tag02 );
		GD.Vertex2ii( x + 71 , y + 6 );
		GD.Vertex2ii( x + 71 + 28 , y + 24 );

		GD.ColorRGB( 0xFFFFFF );
		GD.ColorA( 255 );
		GD.Begin( BITMAPS );
}

void EEPROMWriteFloat( float *num , int MemPos ) {
  byte ByteArray[4];
  memcpy( ByteArray , num , 4 );
  for( int x = 0 ; x < 4 ; x++ ) {
    EEPROM.write( ( MemPos * 4 ) + x, ByteArray[x] );
  }  
}

void EEPROMReadFloat( float *num , int MemPos ) {
  byte ByteArray[4];
  for( int x = 0 ; x < 4 ; x++ ) {
    ByteArray[x] = EEPROM.read( ( MemPos * 4 ) + x );    
  }
  memcpy( num , ByteArray , 4 );
}