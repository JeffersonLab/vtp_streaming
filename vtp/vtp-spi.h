#ifndef VTP_SPI_H
#define VTP_SPI_H
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
 *     Header file for VTP-SPI 
 *
 *----------------------------------------------------------------------------*/


int vtpSPIOpen();
int vtpSPIClose();

void vtpSpiTransfer(int id, uint8_t const *tx, uint8_t const *rx, size_t len);

#endif /* VTP_SPI_H */
