`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
//
// Engineer: Tom zayev
// 
// Create Date: 12/03/2023 09:20:21 PM
//
// Module Name: blinky
//
// Additional Comments:  Classic LED blinking starter project.
//						 Topics learned; - FSM structure, type (mealy & moore)
// 										 - Building and running code on the Zynq PS - gpio access.
//                                       
//////////////////////////////////////////////////////////////////////////////////

module blinky(
    input clk,
    input wire button0, // Software "button" controlled by CPU
	input wire[1:0] btn,	// Physical button 
    output led0_b,
	output led1_g
    );
    
    localparam [1:0] idle = 2'b00,
                     led_on = 2'b01,
                     blink = 2'b10, 
                     rapidblink = 2'b11; 
	
	// signal declaration // 
	
    reg[1:0] state, next_state;
    reg [25:0] count;
    
    reg led0_en;
	reg led1_en;
	reg button1_delay;
	
    wire button0_sync;
    wire button0_debounced;
	wire button1_sync;
    wire button1_debounced;
	
	// submodule instantiation //
	
    sync sync0(clk,button0, button0_sync);
	sync sync1(clk, btn[1], button1_sync);
	
    debounce debounced0(clk, button0_sync, button0_debounced);
	debounce debounced1(clk, button1_sync, button1_debounced);
	
	initial begin
	   state = idle;
	   next_state = idle;
	   count = 0;
	   led1_en = 0;
	end
    
    // state sequencer //
    always @(posedge(clk)) begin
        count <= count + 1;
        state <= next_state;
		
		if(!button1_delay && button1_debounced)
			led1_en = ~led1_en;
		
		button1_delay <= button1_debounced;
		
    end

    // next state logic // 
    always @* begin
        case (state)
            idle: 
                if (button0_debounced) next_state = led_on;  // state changes dictated by SW button0
                else next_state = state;
            
            led_on:
                if (button0_debounced) begin 
                    next_state = blink;
                    count = 0;  // less jarring state switches
                end
                else next_state = state;
            blink: 
                if (button0_debounced) begin
                    next_state = rapidblink;
                    count = 0;
                end
                else next_state = state;
            rapidblink: 
                if (button0_debounced) next_state = idle;
                else next_state = state;
                
            default: next_state <= idle; // Default case for safety
        endcase
    end

    // output logic //
    always @* begin
    case (state)
            idle: begin
                   led0_en = 1'b0;
		    end

            led_on: begin 
                     led0_en = 1'b1;  
		    end

            blink: begin 
                    led0_en = count[25];				 
			end

            rapidblink: begin 
                         led0_en = count[24];					
			end

            default: begin
                      led0_en = 1'b0;
		    end
        endcase
    end
    
    assign led0_b = led0_en;
	assign led1_g = led0_en && led1_en;

endmodule
