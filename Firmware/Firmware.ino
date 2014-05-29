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

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

// Array for storing the flow sensor samples
float WaterFlowValues[SAMPLES];

// Array for storing the temperature samples
float TemperatureValues[SAMPLES];

// Is true if the "Reset" button is pressed, otherwise false
boolean ResetButtonIsPressed = false;

// Is true if the "Settings" icon is pressed, otherwise false
boolean SettingsButtonIsPressed = false;

// Helper variable for taking samples
ulong SampleTimeHelper;

// Helper variable for the number of taken samples
uint NumberOfSamplesHelper = 0;

// Helper variable for the x-axis difference between two values
uint XAxisFactor = 0;

// Counter for the pulses of the flow sensor
volatile ulong FlowSensorPulses = 0;
volatile ulong TotalFlowSensorPulses = 0;

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
	LoadBackground();
	DrawBackground();
	DrawTouchTags();
	GD.swap();

	// Initialize variables
	SampleTimeHelper = millis();
	XAxisFactor = (int)( 222.0 / (float)SAMPLES );

	// Attach an interrupt for the flow sensor
	attachInterrupt( FLOW_SENSOR_PIN , CountImpulses , FALLING) ;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void loop () {

	// Get the input of the touch display and check if something relevant is pressed
	GD.get_inputs();
	if( GD.inputs.tag == 1 ) {
		ResetButtonIsPressed = true;
	} else if( GD.inputs.tag == 2 ) {
		SettingsButtonIsPressed = true;
	} else {
		if( ResetButtonIsPressed == true ) {
			Serial.println( "Reset" );
			delay( 50 );
			ResetButtonIsPressed = false;
		} else if( SettingsButtonIsPressed == true ) {
			Serial.println( "Settings" );
			delay( 50 );
			SettingsButtonIsPressed = false;
		}
	}


	if( millis() - SampleTimeHelper >= SAMPLES_INTERVALL ) {
		float pulsesF = ((float)FlowSensorPulses) / ((float)FLOW_SENSOR_DIVIDER);
		if( NumberOfSamplesHelper < SAMPLES ) {
			WaterFlowValues[ NumberOfSamplesHelper++ ] = pulsesF;
		} else {
			for( uint index = 0 ; index < (SAMPLES-1) ; index++ ) {
				WaterFlowValues[ index ] = WaterFlowValues[ index + 1 ];
			}
			WaterFlowValues[ (SAMPLES-1) ] = pulsesF;
		}
		TotalFlowSensorPulses += FlowSensorPulses;
		FlowSensorPulses = 0;
		DrawBackground();
		DrawTouchTags();
		DrawWaterConsumption();
		GD.swap();
		SampleTimeHelper = millis();
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CountImpulses( ){
	FlowSensorPulses = FlowSensorPulses + 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DrawWaterConsumption(){

	float OffsetFactor = 96.0 / 4.0;

	GD.ColorRGB( 151 , 187 , 205 );
	GD.LineWidth( 1 * 10 );
  	GD.Begin( LINE_STRIP );

  	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) {
  		if( WaterFlowValues[index] != -1.0 ) {
			float xOffsetF = WaterFlowValues[ index ] * OffsetFactor;
			int xOffset = (int)xOffsetF;
			int yOffset = index * XAxisFactor;
			GD.Vertex2ii( 335 + xOffset , 11 + yOffset );
		}
	}

	GD.ColorRGB( 151 , 187 , 205 );
	GD.PointSize( 16 * 2 );
	GD.Begin( POINTS );

	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) {
		if( WaterFlowValues[ index ] != -1.0 ) {
			float xOffsetF = WaterFlowValues[ index ] * OffsetFactor;
			int xOffset = (int)xOffsetF;
			int yOffset = index * XAxisFactor;
			GD.Vertex2ii( 335 + xOffset , 11 + yOffset );
		}
	}

	for( uint16_t index = 0 ; index < (SAMPLES-1) ; index++ ) {
		if( WaterFlowValues[ index + 1 ] != -1.0 ) {
			GD.ColorRGB( 151 , 187 , 205 );
			GD.ColorA( 80 );
			Poly waterFlow;
			waterFlow.begin();
			int xOffset = (int)( WaterFlowValues[ index ] * OffsetFactor );
			int xCordinate = ( 16 * 335 ) + 16 * xOffset;
			int yCordinate = ( 16 * 11 ) + 16 * XAxisFactor * index;
			waterFlow.v( 16 * 335 , yCordinate );
			waterFlow.v( xCordinate , yCordinate );
			xOffset = (int)( WaterFlowValues[ index + 1 ] * OffsetFactor );
			xCordinate = ( 16 * 335 ) + 16 * xOffset;
			yCordinate = ( 16 * 11 ) + 16 * XAxisFactor * ( index + 1 );
			waterFlow.v( xCordinate , yCordinate );
			waterFlow.v( 16 * 335 , yCordinate );
			waterFlow.draw();
		}
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DrawBackground(){
	GD.ClearColorRGB( 0xFFFFFF );
	GD.Clear();
	GD.Begin( BITMAPS );
	GD.Vertex2ii( 0 , 0 );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void LoadBackground(){
	GD.cmd_loadimage( 0 , 0 );
	GD.load( "bg2.jpg" );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void DrawTouchTags(){
	GD.ColorRGB( 0x00FF00 );
	GD.ColorA( 100 );
	GD.Begin( RECTS );
	GD.Tag( 1 );
	GD.Vertex2ii( 7 , 47 );
	GD.Vertex2ii( 47 , 222 );
	GD.Tag( 2 );
	GD.Vertex2ii( 11 , 232 );
	GD.Vertex2ii( 41 , 262 );
}
