/* Microchip Technology Inc. and its subsidiaries.  You may use this software 
 * and any derivatives exclusively with Microchip products. 
 * 
 * THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS".  NO WARRANTIES, WHETHER 
 * EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED 
 * WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A 
 * PARTICULAR PURPOSE, OR ITS INTERACTION WITH MICROCHIP PRODUCTS, COMBINATION 
 * WITH ANY OTHER PRODUCTS, OR USE IN ANY APPLICATION. 
 *
 * IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, 
 * INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND 
 * WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS 
 * BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE.  TO THE 
 * FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS 
 * IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF 
 * ANY, THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
 *
 * MICROCHIP PROVIDES THIS SOFTWARE CONDITIONALLY UPON YOUR ACCEPTANCE OF THESE 
 * TERMS. 
 */

/* 
 * File:   
 * Author: 
 * Comments:
 * Revision history: 
 */

// This is a guard condition so that contents of this file are not included
// more than once.  
//#ifndef XC_HEADER_TEMPLATE_H
//#define	XC_HEADER_TEMPLATE_H

#include <xc.h> // include processor files - each processor file is guarded.  

#define HEX_NEWLINE 0xFDu
#define HEX_COMMA   0xFEu
#define HEX_CR      0xFCu

/* Return codes for hexcsv_push() */
#define HEXCSV_NONE     (-2)  /* no byte produced yet (delimiter/space/partial) */
#define HEXCSV_INVALID  (-1)  /* invalid hex character */
/* Configuration */
#ifndef SDIO_RX_ASCII_CAPACITY
#define SDIO_RX_ASCII_CAPACITY   64u   /* raw ASCII buffer length */
#endif
#ifndef SDIO_FRAME_TOKENS_CAP
#define SDIO_FRAME_TOKENS_CAP    16u   /* parsed tokens capacity (we need 8) */
#endif

/* Minimal status for host to inspect recent parsing state */
typedef enum {
    SDIO_ST_OK              = 0,
    SDIO_ST_SHORT_FRAME     = 1,   /* fewer than 8 tokens before newline */
    SDIO_ST_TOKEN_OVERFLOW  = 2,   /* more than SDIO_FRAME_TOKENS_CAP tokens in a line */
    SDIO_ST_ASCII_OVERFLOW  = 3,   /* raw ASCII buffer overflow */
    SDIO_ST_INVALID_CHAR    = 4    /* hexcsv_push() saw invalid hex char */
} sdio_status_t;




//prototypes


void task_pump_one_uart_byte(void);
bool sdio_word_ready(void);
uint32_t sdio_get_word32(void);
uint8_t CRC5_27b_MSB_poly25(uint32_t payload27);
void task_sdio_frame(void);
void HeartBeat (void);