#ifndef VTP_I2C_H
#define VTP_I2C_H
/*----------------------------------------------------------------------------*
 *  Copyright (c) 2016        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    Authors: Bryan Moffit                                                   *
 *             moffit@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5660             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 * Description:
 *     Header file for VTP-I2C 
 *
 *----------------------------------------------------------------------------*/

int vtpI2COpen();
int vtpI2CClose();

int  vtpI2CSelectSlave(int fd, uint8_t slaveAddr);
uint8_t   vtpI2CRead8(int fd, uint8_t cmd);
uint16_t  vtpI2CRead16(int fd, uint8_t cmd);
uint32_t  vtpI2CReadBlock(int fd, uint8_t cmd, uint8_t *buf);
int  vtpI2CWriteCmd(int id, uint8_t cmd);
int  vtpI2CWrite8(int fd, uint8_t cmd, uint8_t val);
int  vtpI2CWrite16(int fd, uint8_t cmd, uint16_t val);
#endif /* VTP_I2C_H */
