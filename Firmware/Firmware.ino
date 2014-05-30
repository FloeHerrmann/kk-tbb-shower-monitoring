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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Array for storing the flow sensor samples
float WaterFlowValues[SAMPLES];

// Array for storing the temperature samples
float TemperatureValues[SAMPLES];

// Is true if the "Reset" button is pressed, otherwise false
boolean ResetButtonIsPressed = false;

// Is true if the "Settings" icon is pressed, otherwise false
boolean SettingsButtonIsPressed = false;

boolean ShowerIsRunning = false;
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
DeviceAddress WarmWaterSensorAddress = { 0x28, 0xF1 , 0xAC , 0x61 , 0x05 , 0x00 , 0x00 , 0x63 };

// Counter for the impulses of the flow sensor during the last sample interval
volatile ulong FlowSensorPulses = 0;

// Counter the total amount of flow sensor impulses
volatile ulong TotalFlowSensorPulses = 0;

// Setup a OneWire instance to communicate with OneWire devices
OneWire OneWireBus( ONE_WIRE_BUS_PIN );

// Setup a DallesTemperature instance to get temperature from our sensors
DallasTemperature TemperatureSensors( &OneWireBus );

// Current temperature
float CurrentTemperature;

// Current total costs
float CurrentCosts;

// Current total water amount
float CurrentWater;

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void setup() {
	Serial.begin( 57600 );

	// Initialize value arrays
	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) {
		WaterFlowValues[ index ] = -1.0;
		TemperatureValues[ index ] = -1.0;
	}

	// Start the communication with our temperature sensors
	TemperatureSensors.begin();
	TemperatureSensors.setResolution( WarmWaterSensorAddress , 9 );
	TemperatureSensors.requestTemperatures();
	CurrentTemperature = TemperatureSensors.getTempC( WarmWaterSensorAddress );

	CurrentCosts = 0.0;
	CurrentWater = 0.0;

	// Initialize display
	GD.begin();
	LoadImages();
	DrawBackground();
	DrawResetButton();
	DrawTouchTags();
	DrawCosts( CurrentCosts );
	DrawTemperature( CurrentTemperature );
	GD.swap();

	// Initialize variables
	SampleTimeHelper = millis();
	XAxisFactor = (int)( 222.0 / (float)SAMPLES );

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
	} else {
		if( ResetButtonIsPressed == true ) {
			delay( 50 );
			ResetButtonIsPressed = false;
			ResetIsShown = false;
			ResetValues();
		} else if( SettingsButtonIsPressed == true ) {
			Serial.println( "Settings" );
			delay( 50 );
			SettingsButtonIsPressed = false;
		}
	}

	if( ShowerIsRunning == true ) {

		if( millis() - SampleTimeHelper >= SAMPLES_INTERVALL ) {
			float pulsesF = ((float)FlowSensorPulses) / ((float)FLOW_SENSOR_DIVIDER);
			TemperatureSensors.requestTemperatures();
			CurrentTemperature = TemperatureSensors.getTempC( WarmWaterSensorAddress );
			if( NumberOfSamplesHelper < SAMPLES ) {
				WaterFlowValues[ NumberOfSamplesHelper ] = pulsesF;
				TemperatureValues[ NumberOfSamplesHelper ] = CurrentTemperature;
				NumberOfSamplesHelper++;
			} else {
				for( uint index = 0 ; index < (SAMPLES-1) ; index++ ) {
					WaterFlowValues[ index ] = WaterFlowValues[ index + 1 ];
					TemperatureValues[ index ] = TemperatureValues[ index + 1 ];
				}
				WaterFlowValues[ (SAMPLES-1) ] = pulsesF;
				TemperatureValues[ (SAMPLES-1) ] = CurrentTemperature;
			}
			TotalFlowSensorPulses += FlowSensorPulses;
			FlowSensorPulses = 0;
			SampleTimeHelper = millis();
		}
	} else {
		if( millis() - SampleTimeHelper >= SAMPLES_INTERVALL ) {
			TemperatureSensors.requestTemperatures();
			CurrentTemperature = TemperatureSensors.getTempC( WarmWaterSensorAddress );
		}
	}

	CurrentWater = (float)TotalFlowSensorPulses / IMPULSES_PER_LITER;
	CurrentCosts = CurrentWater * 0.04;

	DrawBackground();
	DrawResetButton();
	DrawTouchTags();
	DrawCharts();
	DrawCosts( CurrentCosts );
	DrawTemperature( CurrentTemperature );
	DrawWater( CurrentWater );
	GD.swap();

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CountImpulses( ){
	FlowSensorPulses = FlowSensorPulses + 1;
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
void DrawBackground(){
	GD.ClearColorRGB( 0xFFFFFF );
	GD.Clear();

	GD.Begin(BITMAPS);
	GD.Vertex2ii( 435 , 5 , INTERFACE_HANDLE , 0 );
	GD.Vertex2ii( 282 , 8 , INTERFACE_HANDLE , 1 );
	GD.Vertex2ii( 129 , 7 , INTERFACE_HANDLE , 2 );
	GD.Vertex2ii( 331 , 238 , AXES_HANDLE , 0 );
	GD.Vertex2ii( 178 , 238 , AXES_HANDLE , 1 );
	GD.Vertex2ii( 17 , 239 , SETTINGS_HANDLE );

	DrawChartLines( 182 , 11 );
	DrawChartLines( 335 , 11 );
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
void LoadImages(){
	LOAD_ASSETS();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DrawTouchTags(){
	GD.ColorRGB( 0xFFFFFF );
	GD.ColorA( 0 );
	GD.Begin( RECTS );
	if( ResetIsShown == true ) {
		GD.Tag( 1 );
		GD.Vertex2ii( 7 , 47 );
		GD.Vertex2ii( 47 , 223 );
	}
	GD.Tag( 2 );
	GD.Vertex2ii( 11 , 232 );
	GD.Vertex2ii( 41 , 262 );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DrawResetButton(){
	GD.ColorRGB( 0xFFFFFF );
	GD.Begin(BITMAPS);
	GD.Vertex2ii( 10 , 60 , INTERFACE_HANDLE , 3 );
	if( ResetIsShown == false ) {
		GD.ColorRGB( 0xFFFFFF );
		GD.ColorA( 240 );
		GD.Begin( RECTS );
		GD.Vertex2ii( 7 , 47 );
		GD.Vertex2ii( 47 , 223 );
	} 
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
	uint x = 292;

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
	GD.Vertex2ii( x - 1 , y , SMALL_SIGNS_HANDLE , 2 );
}

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
	uint x = 445;

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