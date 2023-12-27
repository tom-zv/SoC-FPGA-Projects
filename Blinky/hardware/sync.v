`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 12/04/2023 06:41:00 PM
// Design Name: 
// Module Name: sync
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


module sync
#(
    parameter SYNC_BITS = 2  // Number of bits in the synchronisation buffer (2 minimum).
)
(
    input wire clock,
    input wire in,     // Asynchronous input.
    output wire out    // Synchronous output.
);

    localparam SYNC_MSB = SYNC_BITS - 1;

    reg [SYNC_MSB : 0] sync_buffer;

    assign out = sync_buffer[SYNC_MSB];

    always @(posedge clock)
    begin
        sync_buffer[SYNC_MSB : 0] <= {sync_buffer[SYNC_MSB - 1 : 0], in};
    end

endmodule