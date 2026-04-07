/*
 * File:   app.c
 * Author: MPallones
 *
 * Created on March 25, 2026, 6:18 AM
 */


#include "mcc_generated_files/system.h"
#include "mcc_generated_files/mcc.h"
#include "app.h"
#include <string.h>
#define FCY 8000000UL
#include <libpic30.h>
#include<stdio.h>




//variables 



sdio_status_t g_status = SDIO_ST_OK;

/* RX buffers */
uint8_t         readBuffer[SDIO_RX_ASCII_CAPACITY];  /* optional: raw ASCII capture */
uint8_t         frame[SDIO_FRAME_TOKENS_CAP];        /* parsed bytes: {dir,op,addr,HB,LB,rdy,tw,crcerr,...} */
uint16_t        c = 0;       /* raw index */
uint16_t        y = 0;       /* parsed index */
uint8_t         b;
/* Parser state for CSV hex */
typedef struct {
    uint8_t hi;      /* first hex digit (0..15) */
    uint8_t digits;  /* digits seen in current field: 0 or 1 */
} hexcsv_state_t;

static hexcsv_state_t  csv;


/* Output registers & flags */
 volatile uint32_t g_word32      = 0;  /* last computed 32-bit word */
 volatile uint8_t  g_word_ready  = 0;  /* 1 when a new word is available */
 volatile uint32_t g_payload27   = 0;  /* last 27-bit payload (for debug/verify) */
 volatile uint8_t  g_crc5        = 0;  /* last crc5 value (for debug/verify) */

uint8_t  rx_crc_ret=0; 
uint32_t payload_ret = 0;
uint8_t  calc_crc_ret=0;
uint16_t data_ret=0;
uint8_t fail_crc=0;
uint8_t crc_dut_error;
uint32_t frame1;
uint32_t heartbeat=0 ;
 
 


//functions

void HeartBeat (void)
{
    HB_Toggle();
}


 void hexcsv_reset(hexcsv_state_t *s)
{
    s->hi = 0;
    s->digits = 0;
}
 uint8_t hex_to_nibble(uint8_t c)
{
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
    return 0xFF;
}
 
/* Reset line state (called after finalizing or on error) */
void sdio_reset_line_state(void)
{
    hexcsv_reset(&csv);
    y = 0;
    c = 0;
}

int hexcsv_push(hexcsv_state_t *s, uint8_t c)
{
    if (c == ' ' || c == '\t') return HEXCSV_NONE;

    if (c == ',' || c == '\r' || c == '\n' || c == '\0') {
        if (s->digits == 1) {
            uint8_t out = s->hi;   /* single digit -> 0x0H */
            s->digits = 0;
            return (int)out;
        }
        return HEXCSV_NONE;
    }

    uint8_t v = hex_to_nibble(c);
    if (v > 0x0F) {
        s->digits = 0;
        return HEXCSV_INVALID;
    }

    if (s->digits == 0) {
        s->hi = v;
        s->digits = 1;
        return HEXCSV_NONE;
    } else {
        uint8_t out = (uint8_t)((s->hi << 4) | v);
        s->digits = 0;
        return (int)out;
    }
}


/* Build payload27 (bits [26:0]) according to the map above.
 * dir, op, rdy, tw, crcerr are 0/1; addr6 is 6-bit; data16 is 16-bit.
 */
 uint32_t make_payload27_hdr_addr_data(uint8_t dir, uint8_t op,
                                                    uint8_t addr6, uint16_t data16,
                                                    uint8_t rdy, uint8_t tw, uint8_t crcerr)
{
    /* Bit positions inside the 27-bit payload value */
    enum {
        HDR_HI_POS = 26,   // becomes bit 31 after <<5
        HDR_LO_POS = 25,   // becomes bit 30 after <<5
        ADDR_LSB   = 19,   // becomes [29:24] after <<5
        DATA_LSB   = 3,    // becomes [23:8]  after <<5
        RDY_POS    = 2,    // becomes bit 7 after <<5
        TW_POS     = 1,    // becomes bit 6 after <<5
        CRCERR_POS = 0     // becomes bit 5 after <<5
    };

    /* Collapse (dir,op) to hdr2 = 2 bits per spec */
    uint8_t hdr2 = (uint8_t)(((dir & 1u) << 1) | (op & 1u));   // 0..3

    uint32_t p = 0;
    /* header two bits into positions [26:25] (MSB..LSB) */
    p |= (uint32_t)((hdr2 >> 1) & 1u) << HDR_HI_POS;   // hdr2[1] -> [26]
    p |= (uint32_t)( hdr2       & 1u) << HDR_LO_POS;   // hdr2[0] -> [25]

    /* 6-bit address into [24:19] */
    p |= ((uint32_t)(addr6 & 0x3Fu)) << ADDR_LSB;

    /* 16-bit data into [18:3] (HB first on the wire after <<5) */
    p |= ((uint32_t)(data16)) << DATA_LSB;

    /* single-bit flags into [2:0] */
    p |= ((uint32_t)(rdy    & 1u)) << RDY_POS;
    p |= ((uint32_t)(tw     & 1u)) << TW_POS;
    p |= ((uint32_t)(crcerr & 1u)) << CRCERR_POS;

    return p;  /* 27-bit payload value in bits [26:0] */
}

/* CRC-5 for 27-bit payload, MSB-first, init=0x00, xorout=0x00,
 * polynomial full 0x25 (x^5 + x^2 + 1) ? XOR with 0x05 in the loop.
 */
uint8_t CRC5_27b_MSB_poly25(uint32_t payload27)
{
    const uint8_t POLY_NOMSB = 0x05;   // (0x25 & 0x1F)
    uint8_t crc = 0x00;

    for (uint8_t i = 0; i < 27; i++) {
        uint8_t bit = (uint8_t)((payload27 >> (26 - i)) & 1u);  // feed MSB-first
        uint8_t msb = (uint8_t)((crc >> 4) & 1u);               // crc's x^4 term
        crc = (uint8_t)((crc << 1) & 0x1Fu);
        if (msb ^ bit) {
            crc ^= POLY_NOMSB;  // xor with polynomial without MSB term
        }
    }
    return (uint8_t)(crc & 0x1F);
}

/* Final 32-bit word: [ payload27(26:0) << 5 ] | [ CRC5(4:0) ] */
uint32_t make_word32(uint32_t payload27, uint8_t crc5)
{
    return ((payload27 & 0x07FFFFFFu) << 5) | (crc5 & 0x1Fu);
}




/* Internal: finalize when newline is received (CR optional) */
  void sdio_finalize_line(void)
{
    /* We expect exactly 8 tokens:
       frame[0]=dir, frame[1]=op, frame[2]=addr,
       frame[3]=HB, frame[4]=LB, frame[5]=rdy, frame[6]=tw, frame[7]=crcerr
     */
    if (y >= 8u) {
        uint8_t dir    = (uint8_t)(frame[0] & 1u);
        uint8_t op     = (uint8_t)(frame[1] & 1u);
        uint8_t addr6  = (uint8_t)(frame[2] & 0x3Fu);
        uint16_t data16= (uint16_t)(((uint16_t)frame[3] << 8) | frame[4]); /* HB,LB */
        uint8_t rdy    = (uint8_t)(frame[5] & 1u);
        uint8_t tw     = (uint8_t)(frame[6] & 1u);
        uint8_t crcerr = (uint8_t)(frame[7] & 1u);

        uint32_t payload27 = make_payload27_hdr_addr_data(
                                dir, op, addr6, data16, rdy, tw, crcerr);
        uint8_t  crc5      = CRC5_27b_MSB_poly25(payload27);
        uint32_t word32    = make_word32(payload27, crc5);

        g_payload27  = payload27;
        g_crc5       = crc5;
        g_word32     = word32;
        g_word_ready = 1;
        g_status     = SDIO_ST_OK;
    } else {
        /* Not enough tokens for a valid frame */
        g_status = SDIO_ST_SHORT_FRAME;
    }

    /* Reset for next line (success or not) */
    sdio_reset_line_state();
}

  uint32_t sdio_get_word32(void)
{
    g_word_ready = 0;
    return g_word32;
}

/* Public API: check if a new 32-bit word is ready */
bool sdio_word_ready(void)
{
    return (g_word_ready != 0);
}


void task_pump_one_uart_byte(void)
{   
    
  while (UART1_IsRxReady())
  {      
    uint8_t b = UART1_Read();

    // Optional raw capture
    if (c < SDIO_RX_ASCII_CAPACITY) {
        readBuffer[c++] = b;
    } else {
        g_status = SDIO_ST_ASCII_OVERFLOW;
        sdio_reset_line_state();
        c = 0;
    }

    int r = hexcsv_push(&csv, b);
    if (r >= 0) {
        if (y < SDIO_FRAME_TOKENS_CAP) {
            frame[y++] = (uint8_t)r;
        } else {
            g_status = SDIO_ST_TOKEN_OVERFLOW;
            sdio_reset_line_state();
            y = 0;
        }
    } else if (r == HEXCSV_INVALID) {
        g_status = SDIO_ST_INVALID_CHAR;
        // keep waiting for newline
    }

    if (b == '\n') {
        sdio_finalize_line();
        // (optional) set a flag if you want to burst right after a line
        // line_complete = true;
    }
}
}



void task_sdio_frame(void)
{
    
       if (sdio_word_ready())
        {
            /*Retrieve the 32-bit SDIO word (this clears the ready flag) */
            uint32_t w = sdio_get_word32();

            /*TRANSMIT the 32?bit word in SPI MODE32 (MSB-first) */
             SS_SetLow();              // pull CS low
             SPI1_Exchange32bit(w);    // send 32 bits MSB-first ignore data return
 
            frame1 =  SPI1_Exchange32bit(0x00000000);  //
            
             SS_SetHigh();
           
             
             fail_crc = 0xAA;
             rx_crc_ret  = frame1 & 0x1F;
             payload_ret  = (frame1 >> 5) & 0x07FFFFFFu;
             calc_crc_ret =  CRC5_27b_MSB_poly25(payload_ret);
             data_ret = (uint16_t) (frame1>>8);
             crc_dut_error = (uint8_t)(frame1>>5 & 0x01);
             
            if ((calc_crc_ret != rx_crc_ret)|(crc_dut_error == 1 ))
            {
                
                /* ---- CRC FAIL ---- */
                printf("%02X\n",fail_crc);
            }
            else
            {
               
                printf("%04X\n",data_ret);
            }
             
           
    
   }  
    
    
    
    
    
}