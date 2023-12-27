
#include "xgpio.h"
#include "xparameters.h"
#include "platform.h"
//#include <sys/_intsup.h>
//#include <xgpio_l.h>
#include <xstatus.h>
#include "xil_printf.h"


int main()
{
    int status;
    int delay;

    init_platform();

    XGpio btn_output;

    // AXI GPIO initialization
    status = XGpio_Initialize(&btn_output, XPAR_AXI_GPIO_0_BASEADDR);
    if(status != XST_SUCCESS){
        xil_printf("Gpio init failed \n");
        return XST_FAILURE;
    }

    // Code

    XGpio_SetDataDirection(&btn_output, 1, 0);
    while(1){
        
        
        XGpio_DiscreteWrite(&btn_output, 1, 1);

        for (delay = 0; delay < 1.5e5; delay++);

        XGpio_DiscreteWrite(&btn_output, 1, 0);

        for (delay = 0; delay < 5e8; delay++);
    

    }

    
    cleanup_platform();
    return 0;
}