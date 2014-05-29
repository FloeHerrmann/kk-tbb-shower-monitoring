#include <EEPROM.h>
#include <SPI.h>
#include <GD2.h>


#define SAMPLES 20
#define SAMPLESOFFSET 11
#define SAMPLEINTERVALL 3000

#define FLOWSENSORPIN 5

float PulseValues[20];
long SampleStartTime;
int numberOfSamples;

boolean ResetButtonPressed;
boolean SettingsButtonPressed;

volatile uint16_t pulses = 0;

void setup() {
	Serial.begin( 57600 );

	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) PulseValues[ index ] = -1.0;

	GD.begin();
	LoadBackground();
	DrawBackground();
	DrawTouchTags();
	GD.swap();

	pinMode( 16 , OUTPUT );
	digitalWrite( 16 , LOW );
	attachInterrupt( FLOWSENSORPIN , COUNT , FALLING) ;

	SampleStartTime = millis();
	numberOfSamples = 0;
	ResetButtonPressed = false;
	SettingsButtonPressed = false;
}

void loop () {
	GD.get_inputs();
	if( GD.inputs.tag == 1 ) {
		ResetButtonPressed = true;
	} else if( GD.inputs.tag == 2 ) {
		SettingsButtonPressed = true;
	} else {
		if( ResetButtonPressed == true ) {
			Serial.println( "Reset" );
			delay( 50 );
			ResetButtonPressed = false;
		} else if( SettingsButtonPressed == true ) {
			Serial.println( "Settings" );
			delay( 50 );
			SettingsButtonPressed = false;
		}
	}
	
	if( millis() - SampleStartTime >= SAMPLEINTERVALL ) {
		if( numberOfSamples < SAMPLES ) {
			float pulsesF = ((float)pulses)/100.0;
			PulseValues[ numberOfSamples++ ] = pulsesF;
			pulses = 0;
		} else {
			for( uint16_t index = 0 ; index < (SAMPLES-1) ; index++ ) PulseValues[ index ] = PulseValues[ index + 1];
			float pulsesF = ((float)pulses)/100.0;
			PulseValues[ 19 ] = pulsesF;
			pulses = 0;
		}
		DrawBackground();
		DrawTouchTags();
		DrawWaterConsumption();
		GD.swap();
		SampleStartTime = millis();
	}
}

void COUNT( ){
	pulses = pulses + 1;
}

void DrawWaterConsumption(){
	float OffsetFactor = 96.0 / 3.0;

	GD.ColorRGB(151,187,205);
	GD.LineWidth(1 * 10);
  	GD.Begin(LINE_STRIP);
  	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) {
  		if( PulseValues[index] != -1.0 ) {
			float xOffsetF = PulseValues[index] * OffsetFactor;
			int xOffset = (int)xOffsetF;
			int yOffset = index * SAMPLESOFFSET;
			GD.Vertex2ii( 335 + xOffset , 11 + yOffset );
		}
	}

	GD.PointSize(16 * 2);
	GD.ColorRGB(151,187,205);
	GD.Begin(POINTS);
	for( uint16_t index = 0 ; index < SAMPLES ; index++ ) {
		if( PulseValues[index] != -1.0 ) {
			float xOffsetF = PulseValues[index] * OffsetFactor;
			int xOffset = (int)xOffsetF;
			int yOffset = index * SAMPLESOFFSET;
			GD.Vertex2ii( 335 + xOffset , 11 + yOffset );
		}
	}

	//GD.SaveContext();
	for( uint16_t index = 0 ; index < (SAMPLES-1) ; index++ ) {
		if( PulseValues[index+1] != -1.0 ) {
			GD.ColorRGB( 151,187,205 );
			GD.ColorA( 80 );
			Poly waterFlow;
			waterFlow.begin();
			// Point One
			int xOffset = (int)(PulseValues[index] * OffsetFactor);
			int xCordinate = (16 * 335) + 16 * xOffset;
			int yCordinate = (16 * 11) + 16 * SAMPLESOFFSET * index;
			waterFlow.v( 16 * 335 , yCordinate );
			waterFlow.v( xCordinate , yCordinate );
			// Point Two
			xOffset = (int)(PulseValues[index+1] * OffsetFactor);
			xCordinate = (16 * 335) + 16 * xOffset;
			yCordinate = (16 * 11) + 16 * SAMPLESOFFSET * (index+1);
			waterFlow.v( xCordinate , yCordinate );
			waterFlow.v( 16 * 335 , yCordinate );
			waterFlow.draw();
		}
	}
}

void DrawBackground(){
	GD.ClearColorRGB( 0xFFFFFF );
	GD.Clear();
	GD.Begin( BITMAPS );
	GD.Vertex2ii( 0 , 0 );
}

void LoadBackground(){
	GD.cmd_loadimage( 0 , 0 );
	GD.load( "bg2.jpg" );
}

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
