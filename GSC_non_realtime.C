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
#define TAPS1 2 //32
#define beta 0.005
#define eps 0.00001
#define buffer_size 32
#define thresh 0.0643

static float pm const filt_coeffs[TAPS] = {1};

static float pm const filt_coeffs1[TAPS1] = {1, 0};//, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static float state[TAPS + 1];
static float state1[TAPS1 + 1];

static float buffer[buffer_size] = {0.1552,	0.1544,	0.1498,	0.1435,	0.1351,	0.1218,	0.1039,	0.0807,	0.0556,	0.0271,	-0.0032, -0.0333, -0.0628, -0.0901,	-0.1193, -0.1469, -0.1708,	-0.1915, -0.2059, -0.2130,	-0.2100, -0.1998, -0.1850, -0.1676,	-0.1494, -0.1329, -0.1189, -0.1073, -0.1004, -0.0969, -0.0978,	-0.1038};
static float buffer1[buffer_size] = {0.1465, 0.1564, 0.1630, 0.1673, 0.1692, 0.1685, 0.1624, 0.1510, 0.1359, 0.1169, 0.0953, 0.0729,	0.0497,	0.0261,	0.0010, -0.0253, -0.0519, -0.0758, -0.0976, -0.1142, -0.1250, -0.1289, -0.1278, -0.1233, -0.1156, -0.1065, -0.0963, -0.0885, -0.0822, -0.0776, -0.0758, -0.0780};
static float X1[DSP_BLOCK_SIZE];		// temporary input signal
static float X2[DSP_BLOCK_SIZE];		// temporary input signal
static float Y[DSP_BLOCK_SIZE] = {0};			// temporary output signal
static float weights[DSP_BLOCK_SIZE] = {0};
static int enable = 1;
static float frame_count = 0;
static float k = 1.3;

void main()
{   
    // Setup the DSP framework
    //dsp_init();
    int M;
    int VAD = 0;
    int n;
    int i;
    int j;
    int b;
    int A = 3*DSP_BLOCK_SIZE - 2;
    float den = 0;
    float X_in[2*DSP_BLOCK_SIZE-1][DSP_BLOCK_SIZE] = {0};
    float X[DSP_BLOCK_SIZE];
    float x1_filt[DSP_BLOCK_SIZE];
    float x2_filt[DSP_BLOCK_SIZE];
    float x_blocked[DSP_BLOCK_SIZE];
    float x_bt[DSP_BLOCK_SIZE][1];
    float x_a[3*DSP_BLOCK_SIZE-2][1] = {0};
    float x_row[1][DSP_BLOCK_SIZE];
    float x_row_t[DSP_BLOCK_SIZE][1];
    float E[DSP_BLOCK_SIZE];
    float energy = 0;
    float U;
    
    for (M=0; M<1; ++M)
    {
    	for(n=0; n<DSP_BLOCK_SIZE; ++n)
    	{
        	X1[n] = buffer[M*DSP_BLOCK_SIZE+n];
        	X2[n] = buffer1[M*DSP_BLOCK_SIZE+n];
        	X[n] = (X1[n] + X2[n]);//*0.5;
        	energy += (X1[n] * X1[n]);
    	}
    	energy /= DSP_BLOCK_SIZE;
   
	    if(enable == 1)
	    {
	        fir(X1, x1_filt, filt_coeffs, state, DSP_BLOCK_SIZE, TAPS);
	        fir(X2, x2_filt, filt_coeffs1, state1, DSP_BLOCK_SIZE, TAPS1);
	        for(n=0; n<DSP_BLOCK_SIZE; ++n)
	        {
	        	x_blocked[n] = x1_filt[n] + x2_filt[n];
	        	printf("x_blocked(%d) = %f \n x1_filt(%d) = %f \n x2_filt(%d) = %f \n",n,x_blocked[n],n,x1_filt[n],n,x2_filt[n]);
	        }
	        for(n=0; n<DSP_BLOCK_SIZE; ++n)
	        {
	        	x_bt[n][0] = x_blocked[n];
	        	//printf("x_blocked_transpose(%d) = %f \n",n,x_bt[n][0]); 
	        }
	        for(n = 31; n <= 63; ++n)
	        {
	        	x_a[n][0] = x_bt[n-31][0];
	        	//printf("%f\n",x_a[n][0]);
	        }
	        for(i=1; i<=DSP_BLOCK_SIZE; ++i)
	        {
	        	b = 0;
	        	for(j=DSP_BLOCK_SIZE-i+1; j<=A-i+1; ++j)
	        	{
	        		X_in[b][i-1] = x_a[j-1][0];
	        		//printf("X_in[%d][%d] = %f\n",b,i-1,X_in[b][i-1]);
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
		        	Y[i] += (weights[j]*x_row_t[j][0]);
		        	den += (x_row[0][j]*x_row_t[j][0]);
		        }
		        
		        den += eps;
		        U = beta/den;
		        E[i] = X[i] - Y[i];
		        printf("E[%d] = %f \n",i,E[i]);
		        if (VAD == 0)
		        {
			        for(j=0; j<DSP_BLOCK_SIZE; ++j)
			        {
			        	weights[j] += (U*E[i]*x_row[0][j]);
			        }
		        }
	        }
	    }
	    //else
	    //{
	        // Pass-through.
	      //  if (frame_count <= 20)
	    	//{
	    	//	thresh += energy;
	    	//	if (frame_count == 20)
	    	//	{
	    	//		thresh /= frame_count;
	    	//	}
	    	//}
	    //}
	    //frame_count += 1;  
    }
    
    // Enable the DSP framework.
    //dsp_start();
    
    // Everything is handled by the interrupt handlers, so just put an empty
    // idle-loop here. If not, the program falls back to an equivalent idle-loop
    // in the run-time library when main() returns.
    //for(;;)
    //{
    //    idle();
    //}
}
