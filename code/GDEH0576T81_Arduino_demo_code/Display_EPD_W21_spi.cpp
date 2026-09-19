#include "Display_EPD_W21_spi.h"

void SPI_Write(unsigned char value)                                    
{                                                           
    unsigned char i;  
    for(i=0; i<8; i++)   
    {
        EPD_W21_CLK_0;
       if(value & 0x80)
          EPD_W21_MOSI_1;
        else
          EPD_W21_MOSI_0;   
        value = (value << 1); 
        EPD_W21_CLK_1; 
    }
}
//SPI write command
void EPD_W21_WriteCMD(unsigned char command)
{
	EPD_W21_CS_0;
	EPD_W21_DC_0;  // D/C#   0:command  1:data  
	SPI_Write(command);
	EPD_W21_CS_1;
}
//SPI write data
void EPD_W21_WriteDATA(unsigned char datas)
{
	EPD_W21_CS_0;
	EPD_W21_DC_1;  // D/C#   0:command  1:data
	SPI_Write(datas);
	EPD_W21_CS_1;
}
unsigned char EPD_W21_ReadDATA(void)
{
	
	unsigned char i,temp;
   pinMode(23, INPUT);  //SDA IN
    EPD_W21_CS_0;                   
	  EPD_W21_DC_1;		// command write(Must be added)
     for(i=0; i<8; i++)   
    {
    EPD_W21_CLK_0;
		if(EPD_W21_READ==1)    	
	    temp=(temp<<1)|0x01;
		else
      temp=temp<<1;
    EPD_W21_CLK_1; 
    EPD_W21_CLK_0; 
    }
	pinMode(23, OUTPUT);  //SDA OUT
	return(temp);
}