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
#define SAMPLES_INTERVALL 2000

// Interrupt number 5 = Arduino pin 18
#define FLOW_SENSOR_PIN 5

// Pint that is used for the communication with the temperature sensors
#define ONE_WIRE_BUS_PIN 14

// The flow sensor impulses per litre
#define IMPULSES_PER_LITER 165.0

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
#define SCREEN_SETTINGS_WATER 1
#define SCREEN_SETTINGS_ENERGY 2

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Array for storing the flow sensor samples
float WaterFlowValues[SAMPLES];

// Array for storing the temperature samples
float TemperatureWarmValues[SAMPLES];
float TemperatureColdValues[SAMPLES];

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

// If true the "Settings" button will be shown
boolean SettingsIsShown = true;

// Helper variable for taking samples
ulong SampleTimeHelper = 0;

// Helper variable for the timestamp of the last sample
ulong ImpulsesTimeHelper = -1;

// Helper variable for the number of taken samples
uint NumberOfSamplesHelper = 0;

// Helper variable for the x-axis difference between two values
uint XAxisFactor = 0;

// Helper array for the addresses of our temperature sensors
//DeviceAddress WarmWaterSensorAddress = { 0x28, 0xFE , 0x02 , 0x5D , 0x05 , 0x00 , 0x00 , 0x8C };
DeviceAddress WarmWaterSensorAddress = { 0x28, 0xF1 , 0xAC , 0x61 , 0x05 , 0x00 , 0x00 , 0x63 };
DeviceAddress ColdWaterSensorAddress = { 0x28, 0x7A , 0x61 , 0x5D , 0x05 , 0x00 , 0x00 , 0xE5 };

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
volatile float CurrentCosts = 0.0;

// Current total water amount
float CurrentWater = 0.0;

// Current screen that is displayed
ubyte CurrentScreen;

float SettingsWaterCosts = 0.0;
float SettingsEnergyCosts = 0.0;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void setup() {

	// Initialize value arrays
	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) {
		WaterFlowValues[ index ] = -1.0;
		TemperatureWarmValues[ index ] = -1.0;
		TemperatureColdValues[ index ] = -1.0;
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
	CurrentScreen = SCREEN_MAIN;

	DrawMainScreen( CurrentCosts );

	// Attach an interrupt for the flow sensor
	attachInterrupt( FLOW_SENSOR_PIN , CountImpulses , FALLING) ;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void loop () {

	// Get the input of the touch display and check if something relevant is pressed
	GD.get_inputs();
	switch( GD.inputs.tag ) {
		case 101:
			ResetValues();
			SettingsIsShown = true;
			ResetIsShown = false;
			break;
		case 102:
			CurrentScreen = SCREEN_SETTINGS_WATER;
			DrawSettingsScreen();
			break;
		case 103:
			DrawMainScreen( CurrentCosts );
			CurrentScreen = SCREEN_MAIN;
			break;
		case 104:
			EEPROMWriteFloat( &SettingsWaterCosts , 4000 );
			EEPROMWriteFloat( &SettingsEnergyCosts , 4020 );
			CurrentScreen = SCREEN_MAIN;
			DrawMainScreen( CurrentCosts );
			break;
		case 105:
			DrawSettingsScreen();
			CurrentScreen = SCREEN_SETTINGS_WATER;
			break;
		case 106:
			DrawSettingsScreen();
			CurrentScreen = SCREEN_SETTINGS_ENERGY;
			break;
		case 210:
			ManipulateCosts( -100.00 );
			break;
		case 211:
			ManipulateCosts( +100.00 );
			break;
		case 212:
			ManipulateCosts( -10.00 );
			break;
		case 213:
			ManipulateCosts( +10.00 );
			break;
		case 214:
			ManipulateCosts( -1.00 );
			break;
		case 215:
			ManipulateCosts( +1.00 );
			break;
		case 216:
			ManipulateCosts( -0.10 );
			break;
		case 217:
			ManipulateCosts( +0.10 );
			break;
		case 218:
			ManipulateCosts( -0.01 );
			break;
		case 219:
			ManipulateCosts( +0.01 );
			break;
	}

	long timeDifference = millis() - SampleTimeHelper;
	if( timeDifference >= SAMPLES_INTERVALL && CurrentScreen == SCREEN_MAIN ) {
		if( ShowerIsRunning == true ) {

			float pulsesF = ((float)FlowSensorPulses) / ((float)IMPULSES_PER_LITER);
			TemperatureSensors.requestTemperatures();
			CurrentWarmWaterTemperature = TemperatureSensors.getTempC( WarmWaterSensorAddress );
			CurrentColdWaterTemperature = TemperatureSensors.getTempC( ColdWaterSensorAddress );

			float energy = ((pulsesF*1000.0)/float(timeDifference)) * 4.2 * ( CurrentWarmWaterTemperature - CurrentColdWaterTemperature );
			float costs = energy * (float(timeDifference)/3600000.0) * SettingsEnergyCosts;

			CurrentCosts = CurrentCosts + ( pulsesF * SettingsWaterCosts ) + costs;

			pulsesF = pulsesF * (60000/timeDifference);
			if( NumberOfSamplesHelper < SAMPLES ) {
				WaterFlowValues[ NumberOfSamplesHelper ] = pulsesF;
				TemperatureWarmValues[ NumberOfSamplesHelper ] = CurrentWarmWaterTemperature;
				TemperatureColdValues[ NumberOfSamplesHelper ] = CurrentColdWaterTemperature;
				NumberOfSamplesHelper++;
			} else {
				for( uint index = 0 ; index < (SAMPLES-1) ; index++ ) {
					WaterFlowValues[ index ] = WaterFlowValues[ index + 1 ];
					TemperatureWarmValues[ index ] = TemperatureWarmValues[ index + 1 ];
					TemperatureColdValues[ index ] = TemperatureColdValues[ index + 1 ];

				}
				WaterFlowValues[ (SAMPLES-1) ] = pulsesF;
				TemperatureWarmValues[ (SAMPLES-1) ] = CurrentWarmWaterTemperature;
				TemperatureColdValues[ (SAMPLES-1) ] = CurrentColdWaterTemperature;
			}
			TotalFlowSensorPulses += FlowSensorPulses;

			CurrentWater = ((float)TotalFlowSensorPulses) / ((float)IMPULSES_PER_LITER);
		
			FlowSensorPulses = 0;
			SampleTimeHelper = millis();
			DrawMainScreen( CurrentCosts );
		} else {
			TemperatureSensors.requestTemperatures();
			CurrentWarmWaterTemperature = TemperatureSensors.getTempC( WarmWaterSensorAddress );
			CurrentColdWaterTemperature = TemperatureSensors.getTempC( ColdWaterSensorAddress );
			DrawMainScreen( CurrentCosts );
		}
	}

	if( FlowSensorPulses != 0 && ShowerIsRunning == false ) {
		ImpulsesTimeHelper = millis();
		ShowerIsRunning = true;
		ResetIsShown = false;
		SettingsIsShown = false;
	} else {
		if( FlowSensorPulses != 0 ) {
			ImpulsesTimeHelper = millis();
		} else if( millis() - ImpulsesTimeHelper > RESET_BUTTON_TIMEOUT && FlowSensorPulses == 0 && ShowerIsRunning == true ){
			ResetIsShown = true;
			ShowerIsRunning = false;
		} else if( millis() - ImpulsesTimeHelper > VALUES_CLEAR_TIMEOUT && FlowSensorPulses == 0 && ResetIsShown == true ) {
			ResetIsShown = false;
			SettingsIsShown = true;
			ResetValues();
		}
	}

	//delay( 100 );

}

void ManipulateCosts( float value ) {
	if( CurrentScreen == SCREEN_SETTINGS_WATER ) {
		SettingsWaterCosts += value;
		if( SettingsWaterCosts < 0 ) SettingsWaterCosts = 0.0;
		if( SettingsWaterCosts > 999.99 ) SettingsWaterCosts = 999.99;
	} else if( CurrentScreen == SCREEN_SETTINGS_ENERGY ) {
		SettingsEnergyCosts += value;
		if( SettingsEnergyCosts < 0 ) SettingsEnergyCosts = 0.0;
		if( SettingsEnergyCosts > 999.99 ) SettingsEnergyCosts = 999.99;
	}
	DrawSettingsScreen();
	delay( 300 );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CountImpulses( ){
	FlowSensorPulses = FlowSensorPulses + 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void ResetValues(){
	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) {
		WaterFlowValues[ index ] = -1.0;
		TemperatureWarmValues[ index ] = -1.0;
		TemperatureColdValues[ index ] = -1.0;
	}
	FlowSensorPulses = 0;
	TotalFlowSensorPulses = 0;
	NumberOfSamplesHelper = 0;
	CurrentCosts = 0.0;
	SettingsIsShown = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void LoadImages(){
	LOAD_ASSETS();
}

void DrawMainScreen( float TotalCosts ){
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

	// Draw Reset Button
	GD.Vertex2ii( 10 , 60 , INTERFACE_HANDLE , 5 );

	GD.ColorRGB( 0xFFFFFF );
	GD.ColorA( 200 );
	GD.Begin( RECTS );
	if( ResetIsShown == false ) {
		GD.Vertex2ii( 10 , 60 );
		GD.Vertex2ii( 40 , 212 );
	}
	if( SettingsIsShown == false ) {
		GD.Vertex2ii( 7 , 230 );
		GD.Vertex2ii( 39 , 254 );
	}

	// Water Chart
	DrawChartLines( DISPLAY_WIDTH - 140 , MARGIN_LEFT );
	
	// Temperature Chart
	DrawChartLines( DISPLAY_WIDTH - MARGIN_TOP - 286 , MARGIN_LEFT );

	// Draw Touch Tags
	GD.ColorRGB( 0xFFFFFF );
	GD.ColorA( 0 );
	GD.Begin( RECTS );
	if( ResetIsShown == true ) {
		GD.Tag( 101 );
		GD.Vertex2ii( 10 , 60 );
		GD.Vertex2ii( 40 , 212 );
	}
	if( SettingsIsShown == true ) {
		GD.Tag( 102 );
		GD.Vertex2ii( 0 , 226 );
		GD.Vertex2ii( 40 , 272 );
	}

	GD.ColorRGB( 0xFFFFFF );
	GD.ColorA( 255 );
	
	DrawCosts( TotalCosts );
	
	DrawTemperature( CurrentWarmWaterTemperature );
	
	DrawWater( CurrentWater );

	DrawCharts();

	FillCharts();

	GD.swap();
}

void DrawSettingsScreen(){
	GD.ClearColorRGB( 0xFFFFFF );
	GD.Clear();

	float value = 0.0;

	GD.ColorRGB( 0xFFFFFF );
	GD.ColorA( 255 );
	GD.Begin(BITMAPS);
	if( CurrentScreen == SCREEN_SETTINGS_WATER ) {
		GD.Vertex2ii( 421 , 34 , BUTTONS_HANDLE , 1 );
		GD.Vertex2ii( 421 , 144 , BUTTONS_HANDLE , 2 );
		GD.Vertex2ii( 320 , 73 , INTERFACE_HANDLE , 3 );
		value = SettingsWaterCosts;
	} else if( CurrentScreen == SCREEN_SETTINGS_ENERGY ) {
		GD.Vertex2ii( 421 , 34 , BUTTONS_HANDLE , 0 );
		GD.Vertex2ii( 421 , 144 , BUTTONS_HANDLE , 3 );
		GD.Vertex2ii( 320 , 64 , INTERFACE_HANDLE , 4 );
		value = SettingsEnergyCosts;
	}
		
	ubyte hundret = value / 100; value -= (hundret*100);
	ubyte ten = value / 10; value -= (ten*10);
	ubyte one = value / 1; value -= (one*1);
	ubyte tenth = value / 0.1; value -= (tenth*0.1);
	ubyte hundreth = value / 0.01;

	uint offset = 40;

	uint width = 3 * offset + 14 + 40;
	if( hundret != 0 ) width += offset;
	if( ten != 0 ) width += offset;

	uint y = ( 272 - width ) / 2;
	uint x = 228;

	if( hundret != 0 ) {
		GD.Vertex2ii( x , y , BIG_NUMBERS_HANDLE , hundret );
		DrawUpAndDownArrows( x - 25 , y , 210 , 211 );
		y += offset;
	}
	
	if( ten != 0 || hundret != 0 ) {
		GD.Vertex2ii( x , y , BIG_NUMBERS_HANDLE , ten );
		// Up & Down Icons
		DrawUpAndDownArrows( x - 25 , y , 212 , 213 );
		y += offset;
	}
	
	GD.Vertex2ii( x , y , BIG_NUMBERS_HANDLE , one );
	DrawUpAndDownArrows( x - 25 , y , 214 , 215 );
	y += 32;

	GD.Vertex2ii( x - 15 , y , BIG_SIGNS_HANDLE , 0 );
	y += offset/2;

	GD.Vertex2ii( x , y , BIG_NUMBERS_HANDLE , tenth );
	DrawUpAndDownArrows( x - 25 , y , 216 , 217 );
	y += offset;

	GD.Vertex2ii( x , y , BIG_NUMBERS_HANDLE , hundreth );
	DrawUpAndDownArrows( x - 25 , y , 218 , 219 );
	y += offset;

	GD.Vertex2ii( x , y + 2 , BIG_SIGNS_HANDLE , 2 );

	// Back Icon
	GD.Vertex2ii( MARGIN_BOTTOM + 7 , MARGIN_LEFT + 12 , ICONS_HANDLE , 1 );

	GD.Vertex2ii( 10 , 60 , INTERFACE_HANDLE , 6 );

	// Draw Touch Tags
	GD.ColorRGB( 0xFFFFFF );
	GD.ColorA( 0 );
	GD.Begin( RECTS );

	GD.Tag( 103 );
	GD.Vertex2ii( 0 , 0 );
	GD.Vertex2ii( 40 , 45 );
	
	GD.Tag( 104 );
	GD.Vertex2ii( 10 , 60 );
	GD.Vertex2ii( 40 , 212 );

	if( CurrentScreen == SCREEN_SETTINGS_ENERGY ) {
		GD.Tag( 105 );
		GD.Vertex2ii( 421 , 34 );
		GD.Vertex2ii( 449 , 128 );
	}

	if( CurrentScreen == SCREEN_SETTINGS_WATER ) {
		GD.Tag( 106 );
		GD.Vertex2ii( 421 , 144 );
		GD.Vertex2ii( 449 , 238 );
	}

	GD.swap();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DrawCharts(){

	// Water

	float OffsetFactor = 96.0 / 12.0;
	uint xStartCordinate = 340;
	uint yStartCordinate = 11;

	float xOffsetF = 0.0;
	int xOffset = 0;
	int yOffset = 0;

	GD.ColorRGB( 151 , 187 , 205 );
	GD.ColorA( 255 );
	GD.LineWidth( 1 * 10 );
  	GD.Begin( LINE_STRIP );

  	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) {
  		if( WaterFlowValues[index] != -1.0 ) {
			xOffsetF = WaterFlowValues[ index ] * OffsetFactor;
			xOffset = (int)xOffsetF;
			yOffset = index * XAxisFactor;
			GD.Vertex2ii( xStartCordinate + xOffset , yStartCordinate + yOffset );
		}
	}

	GD.ColorRGB( 151 , 187 , 205 );
	GD.ColorA( 255 );
	GD.PointSize( 16 * 2 );
	GD.Begin( POINTS );

	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) {
		if( WaterFlowValues[ index ] != -1.0 ) {
			xOffsetF = WaterFlowValues[ index ] * OffsetFactor;
			xOffset = (int)xOffsetF;
			yOffset = index * XAxisFactor;
			GD.Vertex2ii( xStartCordinate + xOffset , yStartCordinate + yOffset - 1 );
		}
	}

	// Temperature (Warm)

	OffsetFactor = 96.0 / 60.0;
	xStartCordinate = 184;
	yStartCordinate = 11;

	GD.ColorRGB( 208 , 2 , 27 );
	GD.ColorA( 255 );

	GD.LineWidth( 1 * 10 );
  	GD.Begin( LINE_STRIP );

  	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) {
  		if( TemperatureWarmValues[index] != -1.0 ) {
			xOffsetF = TemperatureWarmValues[ index ] * OffsetFactor;
			xOffset = (int)xOffsetF;
			yOffset = index * XAxisFactor;
			GD.Vertex2ii( xStartCordinate + xOffset , yStartCordinate + yOffset );
		}
	}

	// Temperature ( Cold )

	GD.ColorRGB( 151 , 187 , 205 );
	GD.ColorA( 255 );

	GD.LineWidth( 1 * 10 );
  	GD.Begin( LINE_STRIP );

  	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) {
  		if( TemperatureColdValues[index] != -1.0 ) {
			xOffsetF = TemperatureColdValues[ index ] * OffsetFactor;
			xOffset = (int)xOffsetF;
			yOffset = index * XAxisFactor;
			GD.Vertex2ii( xStartCordinate + xOffset , yStartCordinate + yOffset );
		}
	}

	// Temperature (Warm)

	GD.ColorRGB( 208 , 2 , 27 );
	GD.ColorA( 255 );

	GD.PointSize( 16 * 2 );
	GD.Begin( POINTS );

	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) {
		if( TemperatureWarmValues[ index ] != -1.0 ) {
			xOffsetF = TemperatureWarmValues[ index ] * OffsetFactor;
			xOffset = (int)xOffsetF;
			yOffset = index * XAxisFactor;
			GD.Vertex2ii( xStartCordinate + xOffset , yStartCordinate + yOffset );
		}
	}

	// Temperature (Warm)

	GD.ColorRGB( 151 , 187 , 205 );
	GD.ColorA( 255 );

	GD.PointSize( 16 * 2 );
	GD.Begin( POINTS );

	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) {
		if( TemperatureColdValues[ index ] != -1.0 ) {
			xOffsetF = TemperatureColdValues[ index ] * OffsetFactor;
			xOffset = (int)xOffsetF;
			yOffset = index * XAxisFactor;
			GD.Vertex2ii( xStartCordinate + xOffset , yStartCordinate + yOffset );
		}
	}
}

void FillCharts(){

	Poly polygon;

	int xOffset = 0;
	int xCordinate = 0;
	int yCordinate = 0;

	float OffsetFactor = 96.0 / 12.0;
	uint xStartCordinate = 341;
	uint yStartCordinate = 11;

	GD.ColorRGB( 151 , 187 , 205 );
	GD.ColorA( 80 );

	for( uint16_t index = 0 ; index < (SAMPLES-1) ; index++ ) {
		if( WaterFlowValues[ index + 1 ] != -1.0 ) {			
			polygon.begin();

			xOffset = (int)( WaterFlowValues[ index ] * OffsetFactor );
			xCordinate = 16 * xStartCordinate + 16 * xOffset;
			yCordinate = 16 * yStartCordinate + 16 * XAxisFactor * index;

			polygon.v( 16 * ( xStartCordinate + 1 ) , yCordinate + 16 );
			polygon.v( xCordinate , yCordinate + 16 );
			
			xOffset = (int)( WaterFlowValues[ index + 1 ] * OffsetFactor );
			xCordinate = 16 * ( xStartCordinate + 1 ) + 16 * xOffset;
			yCordinate = 16 * yStartCordinate + 16 * XAxisFactor * ( index + 1 );
			
			polygon.v( xCordinate , yCordinate + 16 );
			polygon.v( 16 * xStartCordinate , yCordinate + 16 );
			
			polygon.draw();
		}
	}

	OffsetFactor = 96.0 / 60.0;
	xStartCordinate = 185;
	yStartCordinate = 11;

	// Temperatur (Cold)

	GD.ColorRGB( 151 , 187 , 205 );
	GD.ColorA( 60 );

	for( uint16_t index = 0 ; index < (SAMPLES-1) ; index++ ) {
		if( TemperatureColdValues[ index + 1 ] != -1.0 ) {
			polygon.begin();

			yCordinate = 16 * yStartCordinate + 16 * XAxisFactor * index;

			xOffset = (int)( TemperatureColdValues[ index ] * OffsetFactor );
			xCordinate = 16 * xStartCordinate + 16 * xOffset;
			yCordinate = 16 * yStartCordinate + 16 * XAxisFactor * index;

			polygon.v( 16 * xStartCordinate , yCordinate );
			polygon.v( xCordinate , yCordinate );

			xOffset = (int)( TemperatureColdValues[ index + 1 ] * OffsetFactor );
			xCordinate = 16 * xStartCordinate + 16 * xOffset;
			yCordinate = 16 * yStartCordinate + 16 * XAxisFactor * ( index + 1 );

			polygon.v( xCordinate , yCordinate );
			polygon.v( 16 * xStartCordinate , yCordinate );

			polygon.draw();
		}
	}

	GD.ColorRGB( 208 , 2 , 27 );
	GD.ColorA( 60 );

	int xOffsetCold, xCordinateCold;

	for( uint16_t index = 0 ; index < (SAMPLES-1) ; index++ ) {
		if( TemperatureWarmValues[ index + 1 ] != -1.0 ) {
			polygon.begin();

			xOffsetCold = (int)( TemperatureColdValues[ index ] * OffsetFactor );
			xCordinateCold = 16 * xStartCordinate + 16 * xOffsetCold;

			xOffset = (int)( TemperatureWarmValues[ index ] * OffsetFactor );
			xCordinate = 16 * xStartCordinate + 16 * xOffset;
			yCordinate = 16 * yStartCordinate + 16 * XAxisFactor * index;

			polygon.v( xCordinateCold , yCordinate );
			polygon.v( xCordinate , yCordinate );

			xOffsetCold = (int)( TemperatureColdValues[ index + 1 ] * OffsetFactor );
			xCordinateCold = 16 * xStartCordinate + 16 * xOffsetCold;

			xOffset = (int)( TemperatureWarmValues[ index + 1 ] * OffsetFactor );
			xCordinate = 16 * xStartCordinate + 16 * xOffset;
			yCordinate = 16 * yStartCordinate + 16 * XAxisFactor * ( index + 1 );

			polygon.v( xCordinate , yCordinate );
			polygon.v( xCordinateCold , yCordinate );

			polygon.draw();
		}
	}

	GD.ColorRGB( 0xFFFFFF );
	GD.ColorA( 255 );
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

	// GD.LineWidth( 12 );
	// GD.Vertex2ii( x , y );
	// GD.Vertex2ii( x + 102 , y );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DrawCosts( float valueF ){
	GD.ColorRGB( 0xFFFFFF );
	GD.Begin( BITMAPS );
	
	ulong value = floor( valueF );

	ubyte hundret = floor( value / 10000 ); 
	value = value - hundret * 10000;
	
	ubyte ten = floor( value / 1000 );
	value = value - ten * 1000;
	
	ubyte one = floor( value / 100 );
	value = value - one * 100;
	
	ubyte tenth = floor( value / 10 );
	value = value - tenth * 10;

	ubyte hundreth = floor( value / 1 );
	value = value - hundreth * 1;

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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
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
	uint x = 340;

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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DrawUpAndDownArrows( uint x , uint y , uint tag01 , uint tag02 ) {
	// Up & Down Icons
		GD.Vertex2ii( x + 6 , y + 7 , ICONS_HANDLE , 6 );
		GD.Vertex2ii( x + 77 , y + 7 , ICONS_HANDLE , 7 );

		GD.ColorRGB( 0xFFFFFF );
		GD.ColorA( 0 );
		GD.Begin( RECTS );

		GD.Tag( tag01 );
		GD.Vertex2ii( x , y + 2 );
		GD.Vertex2ii( x + 28 , y + 28 );
		
		GD.Tag( tag02 );
		GD.Vertex2ii( x + 71 , y + 2 );
		GD.Vertex2ii( x + 71 + 28 , y + 28 );

		GD.ColorRGB( 0xFFFFFF );
		GD.ColorA( 255 );
		GD.Begin( BITMAPS );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void EEPROMWriteFloat( float *num , int MemPos ) {
  byte ByteArray[4];
  memcpy( ByteArray , num , 4 );
  for( int x = 0 ; x < 4 ; x++ ) {
    EEPROM.write( ( MemPos * 4 ) + x, ByteArray[x] );
  }  
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void EEPROMReadFloat( float *num , int MemPos ) {
  byte ByteArray[4];
  for( int x = 0 ; x < 4 ; x++ ) {
    ByteArray[x] = EEPROM.read( ( MemPos * 4 ) + x );    
  }
  memcpy( num , ByteArray , 4 );
}