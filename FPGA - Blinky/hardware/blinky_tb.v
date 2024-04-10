`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 12/04/2023 07:08:49 PM
// Design Name: 
// Module Name: blinky_tb
// Project Name: 
// Target Devices: 
// Tool Versions: 
// Description: 
// 
// Dependencies: 
// 
// Revision:
// Revision 0.01 - File Created
// Additional Comments:
// 
//////////////////////////////////////////////////////////////////////////////////


module blinky_tb();

    reg  clk = 0;
    reg  btn = 1'b0;
	wire led0_b;
	
     
    blinky uut(clk, btn, led0_b);

initial begin

    clk = 0;
    
    #20
	
	btn = 1'b1;
    
    #2000
	
	btn = 1'b0;
	
	#2000
	
	btn = 1'b1;
	
	#2000
	
	btn = 1'b0;
	
	#2000
   
 
    end

always #8 clk = ~clk;



endmodule
