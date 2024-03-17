module debounce
#(
    parameter MAX_COUNT = 16
)
(
    input wire clock,
    input wire in,    // Synchronous input.
    output reg rise  
);

    localparam COUNTER_BITS = $clog2(MAX_COUNT);
	
    reg out;
    reg [COUNTER_BITS - 1 : 0] counter;
    wire w_rise;
    

    initial begin
        counter = 0;
        out = 0;
    end

    always @(posedge clock) begin
	  
        rise <= 0;
       
        if (counter == MAX_COUNT - 1) begin  // successfully debounced
            out <= in;
            rise <= w_rise;          
        end
		else if (in == out) begin // reset counter if no change
            counter <= 0;  
        end
        else if (in != out) begin 
            counter <= counter + 1;  // increment when input != output
        end
    end

    assign w_rise = in & ~out; 

endmodule