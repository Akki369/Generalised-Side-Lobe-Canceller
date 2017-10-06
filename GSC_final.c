/*****************************************************************************
 * GSC_final.c
 *****************************************************************************/

#include <processor_include.h>
#include <sysreg.h>
#include <signal.h>
#include <string.h>
#include <filter.h>
#include <stdio.h>
#include <stdlib.h>
#include <matrix.h>

#include "framework.h"

#define TAPS 1
#define TAPS1 2 
#define TAPS2 32
//#define TAPS3 3

//#define beta 0.09
//#define U 0.0001
//#define eps 0.0001
//#define thresh 0.0643

static float pm const filt_coeffs[TAPS] = {1}; 

static float pm const filt_coeffs1[TAPS1] = {-1, 1};//, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

//static float pm const weights3[TAPS3] = {0.0689, 0.8621, 0.0689};

static float state[TAPS +1];
static float state1[TAPS1 + 1];
static float state2[TAPS2 + 1];
//static float state3[TAPS3 + 1];
static float X1[DSP_BLOCK_SIZE];		// temporary input signal
static float X2[DSP_BLOCK_SIZE];        // temporary input signal
static float Y[DSP_BLOCK_SIZE];
//static float Y1[DSP_BLOCK_SIZE];         // temporary output signal
static float weights[DSP_BLOCK_SIZE] = {0.52642852,	-0.099720776,	-0.029272512,	-0.010424641,	-0.043429188,	-0.018581744,	-0.0081713451,	-0.012902254,	-0.0084492778,	0.013879023,	-0.011996891,	0.0073130578,	0.0037660471,	-0.0015861478,	0.0043452303,	0.0037299353,	0.0016172382,	0.0020542310,	0.0027153292,	-0.00034590173,	-0.0028329794,	0.000053106076,	-0.0021982705,	0.0016505248,	0.0027538491,	0.00046264016,	-0.00038838881,	-0.00079098536,	-0.0010113416,	-0.0010119828,	-0.0024551968,	0.00025819265};
static float weights1[DSP_BLOCK_SIZE] = {0};
static float pm weights2[TAPS2];
//static float thresh = 0;
static int enable=1;
static float frame_count = 0;
//static float k = 1;

void process(int sig)
{
	memset(state, 0, sizeof(state));
    memset(state1, 0, sizeof(state1));
    memset(state2, 0, sizeof(state2)); 
	//int VAD = 0;
    int n;
    //int i;
    //int j;
    //int b;
    //int A = 3*DSP_BLOCK_SIZE - 2;
    //float den = 0;
    //float X_in[2*DSP_BLOCK_SIZE-1][DSP_BLOCK_SIZE];
    float X[DSP_BLOCK_SIZE];
    float x1_filt[DSP_BLOCK_SIZE];
    float x2_filt[DSP_BLOCK_SIZE];
    float x_blocked[DSP_BLOCK_SIZE];
    //float x_bt[DSP_BLOCK_SIZE][1];
    //float x_a[3*DSP_BLOCK_SIZE-2][1];
    //float x_row[1][DSP_BLOCK_SIZE];
    //float x_row_t[DSP_BLOCK_SIZE][1];
    float E[DSP_BLOCK_SIZE];
    //float energy = 0;
    //float U;
    
    //memset(X_in,0,sizeof(X_in));
    //memset(x_a,0,sizeof(x_a));
    
    // Get a pointer to the current audio block.
    sample_t *audioin  = dsp_get_audio();
    sample_t *audioout = dsp_get_audio();
    
    // Copy audio from left channel to input buffer. Audio samples are 32 bit
    // fixed-point values in the range [-1, 1] so no additional scaling is required.
    
    for(n=0; n<DSP_BLOCK_SIZE; ++n)
    {
        X1[n] = audioin[n].left;
        X2[n] = audioin[n].right;
        X[n] = (X1[n] + 0.5*X2[n]);
        //energy += (X1[n] * X1[n]);
        weights1[DSP_BLOCK_SIZE-n]= weights[n];
        weights2[n] = weights1[DSP_BLOCK_SIZE-n];
    }
    
    //energy /= DSP_BLOCK_SIZE;
   
    if(enable == 1) //&& (frame_count > 20))
    {
        //if (energy > (k * thresh))
        //{
        //	VAD = 1;
        //}
        
        fir(X1, x1_filt, filt_coeffs, state, DSP_BLOCK_SIZE, TAPS);
        fir(X2, x2_filt, filt_coeffs1, state1, DSP_BLOCK_SIZE, TAPS1);
        
        for(n=0; n<DSP_BLOCK_SIZE; ++n)
        {
        	x_blocked[n] = x1_filt[n] - x2_filt[n];
        }
        
        fir(x_blocked, Y, weights2, state2, DSP_BLOCK_SIZE, TAPS2);
        
        for (n=0; n<DSP_BLOCK_SIZE; ++n)
        {
        	E[n] = X[n] - Y[n];
        }
        
       //fir(Y, Y1, weights3, state3, DSP_BLOCK_SIZE, TAPS3);
        /*for(n=0; n<DSP_BLOCK_SIZE; ++n)
        {
        	x_bt[n][0] = x_blocked[n];
        }
        for(n = 31; n <= 63; ++n)
        {
        	x_a[n][0] = x_bt[n-31][0];
        }
        
        for(i=0; i<DSP_BLOCK_SIZE; ++i)
        {
        	b = 0;
        	for(j=DSP_BLOCK_SIZE-i+1; j<=A-i+1; ++j)
        	{
        		X_in[b][i-1] = x_a[j-1][0];
        		b += 1;
        	}
        }
        for(i=0; i<DSP_BLOCK_SIZE; ++i)
        {
	        for(j=0; j<DSP_BLOCK_SIZE; ++j)
	        {
	        	x_row[0][j] = X_in[i][j];
	        	x_row_t[j][0] = X_in[i][j];
	        }
	        for(j=0; j<DSP_BLOCK_SIZE; ++j)
	        {
	        	Y[i] += (weights2[j]*x_row_t[j][0]);
	        	//den += (x_row[0][j]*x_row_t[j][0]);
	        }
	        //den += eps;
	        //U = beta/den;
	        //den = 0;
	        E[i] = X[i] - Y[i];
	        //if (VAD == 0)
	        //{
		    //for(j=0; j<DSP_BLOCK_SIZE; ++j)
		    //{
		      //  weights[j] += (U*E[i]*x_row[0][j]);
		       	//printf("%f \n", weights[j]);
		    //}
	        //}
	        //printf("\n");
        }*/
        for(n=0; n<DSP_BLOCK_SIZE; ++n) 
    	{
        	audioout[n].left = E[n];
        	audioout[n].right = E[n];
    	}
    }
    else
    {
        // Pass-through.
        //memcpy(Y, X1, sizeof(Y));
        
        /*if (frame_count <= 20)
    	{
    		thresh += energy;
    		if (frame_count == 20)
    		{
    			thresh /= frame_count;
    		}
    	}*/
        for(n=0; n<DSP_BLOCK_SIZE; ++n) 
    	{
        	audioout[n].left = X1[n];
        	audioout[n].right = X2[n];
    	}
    }
    frame_count += 1;
    //printf("%d \n", frame_count);
}

static void keyboard(int sig)
{
    unsigned int keys = dsp_get_keys();
    
    if(keys & 1)
    {
        enable = 1;
        dsp_set_leds(0x1);
    } 
    else if(keys & 2)
    {
        enable = 0;
        dsp_set_leds(0x2);
    }
}

void main()
{   
       
	//memset(state3, 0, sizeof(state3));
    //memset(weights, 0.03, sizeof(weights));
    // Setup the DSP framework
    dsp_init();
    // Register interrupt handlers:
    // SIG_SP1: the audio callback
    // SIG_USR0: the keyboard callback
    // SIG_TMZ: the timer callback
    interrupt(SIG_SP1, process);
    interrupt(SIG_USR0, keyboard);
    // Enable the DSP framework.
    dsp_start();
    // Everything is handled by the interrupt handlers, so just put an empty
    // idle-loop here. If not, the program falls back to an equivalent idle-loop
    // in the run-time library when main() returns.
    for(;;)
    {
        idle();
    }
}
