#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "vtpLib.h"

#define I2C_BUS                  1


#define LTM4676_CMD_PAGE		0x00
#define LTM4676_CMD_READ_VOUT_MODE	0x20
#define LTM4676_CMD_READ_VIN		0x88
#define LTM4676_CMD_READ_VOUT		0x8B
#define LTM4676_CMD_READ_IIN		0x89
#define LTM4676_CMD_READ_IIN_CH	        0xED
#define LTM4676_CMD_READ_IOUT		0x8C
#define LTM4676_CMD_READ_TEMP1	        0x8D
#define LTM4676_CMD_READ_TEMP2	        0x8E
#define LTM4676_CMD_READ_POUT		0x96
#define LTM4676_CMD_READ_MFG		0x99
#define LTM4676_CMD_READ_MODEL	        0x9A

#define LTM4676_CMD_VOUT_OV_FAULT_RESPONSE  0x41
#define LTM4676_CMD_VOUT_UV_FAULT_RESPONSE  0x45

#define LTM4674_CMD_STATUS_BYTE         0x78
#define LTM4674_CMD_STATUS_WORD         0x78
#define LTM4676_CMD_STATUS_VOUT         0x7A
#define LTM4676_CMD_STATUS_IOUT         0x7B

#define LTM4676_1_I2C_SLAVEADDR	0x40
#define LTM4676_2_I2C_SLAVEADDR	0x4D
#define LTM4676_3_I2C_SLAVEADDR	0x4E
#define LTM4676_4_I2C_SLAVEADDR	0x4F

unsigned char LTM4676_ADDR[] = {
  LTM4676_1_I2C_SLAVEADDR,	// LTM4676A: Ch0:     +1.2V (MGT_AVTT), Ch1: +2.2V
  LTM4676_2_I2C_SLAVEADDR,	// LTM4676A: Ch0+Ch1: +1.0V (MGT_AVCC)
  LTM4676_3_I2C_SLAVEADDR,	// LTM4676A: Ch0+Ch1: +1.0V (VCCINT)
  LTM4676_4_I2C_SLAVEADDR		// LTM4676A: Ch0:     +3.3V,            Ch1: +1.5V (VDD_DDR)
};

const char *rail[] = {
  "+1.20V(MGT_AVTT)",
  "+2.20V          ",
  "+1.05V(MGT_AVCC)",
  "+1.05V(MGT_AVCC)",
  "+1.00V(VCCINT)  ",
  "+1.00V(VCCINT)  ",
  "+3.30V          ",
  "+1.50V(VDD_DDR) "
};

void exit_error(const char *func, int retval)
{
  printf("  from %s errno = %d\n",
	 func, retval);
}

void ltm4676_select_slave(int slaveAddr)
{
  if(vtpI2CSelectSlave(I2C_BUS, (uint8_t)slaveAddr) < 0)
    exit_error(__func__, 1);
}

void ltm4676_set_page(char page)
{
  if(vtpI2CWrite8(I2C_BUS, LTM4676_CMD_PAGE, page) < 0)
    {
      exit_error(__func__, page);
    }
}

void ltm4676_write_byte(unsigned char slaveAddr, int page, unsigned char cmd, unsigned char data)
{
  int32_t rval;

  vtpI2CSelectSlave(I2C_BUS, slaveAddr);

  if(page >= 0)
    ltm4676_set_page(page);

  if((rval = vtpI2CWrite8(I2C_BUS, cmd, data)) < 0)
    exit_error(__func__, 1);  

  return;
}

unsigned char ltm4676_read_byte(unsigned char slaveAddr, int page, unsigned char cmd)
{
  int32_t rval;

  vtpI2CSelectSlave(I2C_BUS, slaveAddr);

  if(page >= 0)
    ltm4676_set_page(page);

  if((rval = vtpI2CRead16(I2C_BUS, cmd)) < 0)
    exit_error(__func__, 1);  

  return (rval & 0xFF);
}

unsigned short ltm4676_read_word(unsigned char slaveAddr, int page, unsigned char cmd)
{
  int32_t rval;

  vtpI2CSelectSlave(I2C_BUS, slaveAddr);
  
  if(page >= 0)
    ltm4676_set_page(page);

  if((rval = vtpI2CRead16(I2C_BUS, cmd)) < 0)
    exit_error(__func__, 1);	

  return (rval & 0xFFFF);
}

void ltm4676_read_block(unsigned char slaveAddr, int page, unsigned char cmd, unsigned char *buf)
{
  int32_t rval;

  vtpI2CSelectSlave(I2C_BUS, slaveAddr);

  if(page >= 0)
    ltm4676_set_page(page);

  if((rval = vtpI2CReadBlock(I2C_BUS, cmd, buf)) < 0)
    exit_error(__func__, 1);	
}

float get_L11(unsigned short v)
{
  int N, Y;
  float result;

  if(v & 0x8000)	N = ((v>>11) & 0x1F) | 0xFFFFFFE0;
  else			N = ((v>>11) & 0x1F);

  if(v & 0x0400)	Y = ((v>>0) & 0x7FF) | 0xFFFFF800;
  else			Y = ((v>>0) & 0x7FF);

  result = (float)Y * powf(2.0, (float)N);

  return result;
}

float get_L16(unsigned short v)
{
  float result;

  result = (float)v / 4096.0;

  return result;
}

float get_vin(int chip)
{
  unsigned short v = ltm4676_read_word(LTM4676_ADDR[chip], -1, LTM4676_CMD_READ_VIN);
  return get_L11(v);
}

float get_vout_ch(int chip, int channel)
{
  unsigned short v = ltm4676_read_word(LTM4676_ADDR[chip], channel, LTM4676_CMD_READ_VOUT);
  return get_L16(v);
}

float get_iin(int chip)
{
  unsigned short v = ltm4676_read_word(LTM4676_ADDR[chip], -1, LTM4676_CMD_READ_IIN);
  return get_L11(v);
}

float get_iin_ch(int chip, int channel)
{
  unsigned short v = ltm4676_read_word(LTM4676_ADDR[chip], channel, LTM4676_CMD_READ_IIN_CH);
  return get_L11(v);
}

float get_iout_ch(int chip, int channel)
{
  unsigned short v = ltm4676_read_word(LTM4676_ADDR[chip], channel, LTM4676_CMD_READ_IOUT);
  return get_L11(v);
}

float get_temp(int chip)
{
  unsigned short v = ltm4676_read_word(LTM4676_ADDR[chip], -1, LTM4676_CMD_READ_TEMP2);
  return get_L11(v);
}

float get_temp_ch(int chip, int channel)
{
  unsigned short v = ltm4676_read_word(LTM4676_ADDR[chip], channel, LTM4676_CMD_READ_TEMP1);
  return get_L11(v);
}

float get_power_ch(int chip, int channel)
{
  unsigned short v = ltm4676_read_word(LTM4676_ADDR[chip], channel, LTM4676_CMD_READ_POUT);
  return get_L11(v);
}

//#define LTM4676_CMD_VOUT_OV_FAULT_RESPONSE  0x41
//#define LTM4676_CMD_VOUT_UV_FAULT_RESPONSE  0x45

unsigned char get_status_byte(int chip, int channel)
{
  return ltm4676_read_byte(LTM4676_ADDR[chip], channel, LTM4674_CMD_STATUS_BYTE);
}

unsigned char get_status_word(int chip, int channel)
{
  return ltm4676_read_word(LTM4676_ADDR[chip], channel, LTM4674_CMD_STATUS_WORD);
}

unsigned char get_status_vout(int chip, int channel)
{
  return ltm4676_read_byte(LTM4676_ADDR[chip], channel, LTM4676_CMD_STATUS_VOUT);
}

unsigned char get_status_iout(int chip, int channel)
{
  return ltm4676_read_byte(LTM4676_ADDR[chip], channel, LTM4676_CMD_STATUS_IOUT);
}

char *get_mfg_id(int chip)
{
  unsigned char buf[32];
  static char id[11];

  memset(buf, 0, sizeof(buf));
  memset(id, 0, sizeof(id));
  ltm4676_read_block(LTM4676_ADDR[chip], -1, LTM4676_CMD_READ_MFG, buf);
  /* if(buf[0] < sizeof(id)-1) */
    {
      memcpy(id, buf, sizeof(id));
      return &id[0];
    }

  return NULL;
}

char *get_part_id(int chip)
{
  unsigned char buf[32];
  static char id[11];

  memset(buf, 0, sizeof(buf));
  memset(id, 0, sizeof(id));
  ltm4676_read_block(LTM4676_ADDR[chip], -1, LTM4676_CMD_READ_MODEL, buf);
  /* if(buf[0] < sizeof(id)-1) */
    {
      memcpy(id, buf, sizeof(id));
      return &id[0];
    }
  return NULL;
}

void ltm4676_print_status()
{
  int i, ch;

  for(i = 0; i < 4; i++)
    {
      printf("LTM4676A %d, SLAVE_ADDR %02X\n", i, LTM4676_ADDR[i]);
      printf("   Mfg Id = %s, Part Id = %s\n", get_mfg_id(i), get_part_id(i));
      printf("   VIN = %5.3fV, IIN = %5.3fA, CTRL TEMP = %5.3fC\n", get_vin(i), get_iin(i), get_temp(i));
    
      for(ch = 0; ch < 2; ch++)
      {
        unsigned short status_word = get_status_word(i, ch);
        unsigned char status_vout = get_status_vout(i, ch);
        unsigned char status_iout = get_status_iout(i, ch);
        
        printf("      %s: VOUT = %5.3fV, IOUT = %5.3fA, TEMP = %5.3fC, IIN = %5.3fA, POWER = %5.3fW\n", rail[2*i+0],
            get_vout_ch(i,0), get_iout_ch(i,0), get_temp_ch(i,0), get_iin_ch(i,0), get_power_ch(i,0));
        printf("      %s: VOUT = %5.3fV, IOUT = %5.3fA, TEMP = %5.3fC, IIN = %5.3fA, POWER = %5.3fW\n",rail[2*i+1],
            get_vout_ch(i,1), get_iout_ch(i,1), get_temp_ch(i,1), get_iin_ch(i,1), get_power_ch(i,1));
            
        printf("          STATUS WORD (fault source): 0x%04X\n", status_word);
        if(status_word & 0x8000) printf("          VOUT\n");
        if(status_word & 0x4000) printf("          IOUT\n");
        if(status_word & 0x2000) printf("          INPUT\n");
        if(status_word & 0x1000) printf("          MFR_SPECIFIC\n");
        if(status_word & 0x0800) printf("          POWER_GOOD#\n");
        if(status_word & 0x0400) printf("          FANS\n");
        if(status_word & 0x0200) printf("          OTHER\n");
        if(status_word & 0x0100) printf("          UNKNOWN\n");
        if(status_word & 0x0080) printf("          BUSY\n");
        if(status_word & 0x0040) printf("          OFF\n");
        if(status_word & 0x0020) printf("          VOUT_OV\n");
        if(status_word & 0x0010) printf("          IOUT_OC\n");
        if(status_word & 0x0008) printf("          VIN_UV\n");
        if(status_word & 0x0004) printf("          TEMPERATURE\n");
        if(status_word & 0x0002) printf("          CML\n");
        if(status_word & 0x0001) printf("          OTHER\n");
        
        printf("          STATUS VOUT (fault source): 0x%02X\n", status_vout);
        if(status_vout & 0x0080) printf("          VOUT_OV FAULT\n");
        if(status_vout & 0x0040) printf("          VOUT_OV WARNING\n");
        if(status_vout & 0x0020) printf("          VOUT_UV WARNING\n");
        if(status_vout & 0x0010) printf("          VOUT_UV FAULT\n");
        if(status_vout & 0x0008) printf("          VOUT_MAX WARNING\n");
        if(status_vout & 0x0004) printf("          TON_MAX FAULT\n");
        if(status_vout & 0x0002) printf("          TOFF_MAX WARNING\n");
        if(status_vout & 0x0001) printf("          NOT SUPPORTED\n");
        
        printf("          STATUS IOUT (fault source): 0x%02X\n", status_iout);
        if(status_iout & 0x0080) printf("          IOUT_IC FAULT\n");
        if(status_iout & 0x0040) printf("          NOT SUPPORTED\n");
        if(status_iout & 0x0020) printf("          OUT_OC WARNING\n");
        if(status_iout & 0x0010) printf("          NOT SUPPORTED\n");
        if(status_iout & 0x0008) printf("          NOT SUPPORTED\n");
        if(status_iout & 0x0004) printf("          NOT SUPPORTED\n");
        if(status_iout & 0x0002) printf("          NOT SUPPORTED\n");
        if(status_iout & 0x0001) printf("          NOT SUPPORTED\n");
        
        printf("         VOUT_OV_FAULT_RESPONSE = 0x%02X\n", 
               ltm4676_read_byte(LTM4676_ADDR[i], ch, 0x41));
               
        printf("         VOUT_UV_FAULT_RESPONSE = 0x%02X\n", 
               ltm4676_read_byte(LTM4676_ADDR[i], ch, 0x45));
      }
    }
}

void ltm4676_setup()
{
  int i, ch;
  printf("Before:\n");
  for(i = 0; i < 4; i++)
  {
    for(ch = 0; ch < 2; ch++)
    {
      printf("         VOUT_OV_FAULT_RESPONSE = 0x%02X\n", ltm4676_read_byte(LTM4676_ADDR[i], ch, 0x41));
      printf("         VOUT_UV_FAULT_RESPONSE = 0x%02X\n", ltm4676_read_byte(LTM4676_ADDR[i], ch, 0x45));
    }
  }

  for(i = 0; i < 4; i++)
  {
    for(ch = 0; ch < 2; ch++)
    {
  //    if( (i == 0) && (ch == 1) ) // 2.2v output
        ltm4676_write_byte(LTM4676_ADDR[i], ch, 0x45, 0); // VOUT UV ignore fault
        ltm4676_write_byte(LTM4676_ADDR[i], ch, 0x41, 0); // VOUT OV ignore fault
//      else
//        ltm4676_write_byte(LTM4676_ADDR[i], ch, 0x45, 0xB8); // VOUT UV reset on fault
    }
  }
  
  printf("After:\n");
  for(i = 0; i < 4; i++)
  {
    for(ch = 0; ch < 2; ch++)
    {
      printf("         VOUT_OV_FAULT_RESPONSE = 0x%02X\n", ltm4676_read_byte(LTM4676_ADDR[i], ch, 0x41));
      printf("         VOUT_UV_FAULT_RESPONSE = 0x%02X\n", ltm4676_read_byte(LTM4676_ADDR[i], ch, 0x45));
    }
  }
  
  printf("Storing to eeprom:\n");
  for(i = 0; i < 4; i++)
  {
    vtpI2CSelectSlave(I2C_BUS, LTM4676_ADDR[i]);
    vtpI2CWriteCmd(I2C_BUS, 0x15);
  }  
}

int main(int argc, char *argv[])
{
  if(vtpCheckAddresses() == ERROR)
    exit(-1);

  if(vtpOpen(VTP_I2C_OPEN) == ERROR)
    {
      printf("vtpOpen not OK\n");
      goto CLOSE;
    }

  if(argc == 2)  
    vtpZ7CfgLoad("../firmware/z7_top_wrapper.bin");

  ltm4676_setup();
    
  ltm4676_print_status();

 CLOSE:
  vtpClose(VTP_I2C_OPEN);


  exit(0);
}
