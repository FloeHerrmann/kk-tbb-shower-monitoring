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
#define SAMPLES_INTERVALL 3000

// Interrupt number 5 = Arduino pin 18
#define FLOW_SENSOR_PIN 5

// Divider to get the water amout from the flow sensor impulses
#define FLOW_SENSOR_DIVIDER 100

// Pint that is used for the communication with the temperature sensors
#define ONE_WIRE_BUS_PIN 14

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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void setup() {
	Serial.begin( 57600 );

	// Initialize value arrays
	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) {
		WaterFlowValues[ index ] = -1.0;
		TemperatureValues[ index ] = -1.0;
	}

	// Initialize display
	GD.begin();
	LoadImages();
	DrawBackground();
	DrawResetButton();
	DrawTouchTags();
	GD.swap();

	// Initialize variables
	SampleTimeHelper = millis();
	XAxisFactor = (int)( 222.0 / (float)SAMPLES );

	// Start the communication with our temperature sensors
	TemperatureSensors.begin();
	TemperatureSensors.setResolution( WarmWaterSensorAddress , 9 );

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
	} else if( millis() - ImpulsesTimeHelper > (10*1000) && FlowSensorPulses == 0 && ShowerIsRunning == true ){
		ResetIsShown = true;
		ShowerIsRunning = false;
	} else if( millis() - ImpulsesTimeHelper > (20*1000) && FlowSensorPulses == 0 && ResetIsShown == true ) {
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
			float temperatureF = TemperatureSensors.getTempC( WarmWaterSensorAddress );
			if( NumberOfSamplesHelper < SAMPLES ) {
				WaterFlowValues[ NumberOfSamplesHelper ] = pulsesF;
				TemperatureValues[ NumberOfSamplesHelper ] = temperatureF;
				NumberOfSamplesHelper++;
			} else {
				for( uint index = 0 ; index < (SAMPLES-1) ; index++ ) {
					WaterFlowValues[ index ] = WaterFlowValues[ index + 1 ];
					TemperatureValues[ index ] = TemperatureValues[ index + 1 ];
				}
				WaterFlowValues[ (SAMPLES-1) ] = pulsesF;
				TemperatureValues[ (SAMPLES-1) ] = temperatureF;
			}
			TotalFlowSensorPulses += FlowSensorPulses;
			FlowSensorPulses = 0;
			SampleTimeHelper = millis();
		}
	}

	DrawBackground();
	DrawResetButton();
	DrawTouchTags();
	DrawCharts();
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
	if( ResetIsShown == true ) {
		GD.ColorRGB( 0xFFFFFF );
		GD.Begin(BITMAPS);
		GD.Vertex2ii( 10 , 60 , INTERFACE_HANDLE , 3 );
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
}