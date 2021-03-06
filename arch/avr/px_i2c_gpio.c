/* =============================================================================
     ____    ___    ____    ___    _   _    ___    __  __   ___  __  __ TM
    |  _ \  |_ _|  / ___|  / _ \  | \ | |  / _ \  |  \/  | |_ _| \ \/ /
    | |_) |  | |  | |     | | | | |  \| | | | | | | |\/| |  | |   \  /
    |  __/   | |  | |___  | |_| | | |\  | | |_| | | |  | |  | |   /  \
    |_|     |___|  \____|  \___/  |_| \_|  \___/  |_|  |_| |___| /_/\_\

    Copyright (c) 2012 Pieter Conradie <https://piconomix.com>
 
    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
    deal in the Software without restriction, including without limitation the
    rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
    sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.

    Title:          px_i2c.h : Bit bang I2C master driver
    Author(s):      Pieter Conradie
    Creation Date:  2012-06-25

============================================================================= */

/* _____STANDARD INCLUDES____________________________________________________ */

/* _____PROJECT INCLUDES_____________________________________________________ */
#include "px_i2c.h"
#include "px_board.h"

/* _____LOCAL DEFINITIONS____________________________________________________ */
/// Definition of data for each I2C peripheral
typedef struct px_i2c_data_s
{
    /// Peripheral
    px_i2c_per_t peripheral;
    /// Number of open handles referencing peripheral
    uint8_t open_counter;
} px_i2c_data_t;

/// I2C Start transaction with Read (1) or Write (0) bit
typedef enum
{
    PX_I2C_START_W = 0,
    PX_I2C_START_R = 1,    
} px_i2c_start_t;

/* _____MACROS_______________________________________________________________ */

/* _____GLOBAL VARIABLES_____________________________________________________ */

/* _____LOCAL VARIABLES______________________________________________________ */
/// Allocate data for each enabled I2C peripheral
#if PX_I2C_CFG_I2C0_EN
static px_i2c_data_t px_i2c0_data;
#endif

/* _____LOCAL FUNCTION DECLARATIONS__________________________________________ */
static void px_i2c_delay_half_clk                   (void);
static bool px_i2c_scl_set_hiz_and_wait_clk_stretch (void);
static bool px_i2c_tx_bit                           (bool bit);
static bool px_i2c_rx_bit                           (bool * bit);
static bool px_i2c_start                            (uint8_t sla_adr, 
                                                     px_i2c_start_t start);
static bool px_i2c_start_repeat                     (uint8_t sla_adr, 
                                                     px_i2c_start_t start);
static bool px_i2c_stop                             (void);
static bool px_i2c_wr_u8                            (uint8_t data);
static bool px_i2c_rd_u8                            (uint8_t * data, bool nak);
static void px_i2c_init_peripheral_data             (px_i2c_per_t    peripheral,
                                                     px_i2c_data_t * i2c_data);

/* _____LOCAL FUNCTIONS______________________________________________________ */
static void px_i2c_delay_half_clk(void)
{
    px_board_delay_us(PX_UDIV_ROUND(1E6, 2 * PX_I2C_CFG_BIT_RATE_HZ));
}

static bool px_i2c_scl_set_hiz_and_wait_clk_stretch(void)
{
    uint8_t i;

    PX_I2C_CFG_SCL_SET_HIZ();
    for(i=100; i != 0; i--)
    {
        px_i2c_delay_half_clk();
        if(PX_I2C_CFG_SCL_IS_HI())
        {
            return true;
        }        
    }
    // Timed out waiting for SCL to go high
    return false;
}

static bool px_i2c_tx_bit(bool bit)
{
    // Output bit
    if(bit)
    {
        PX_I2C_CFG_SDA_SET_HIZ();
    }
    else
    {
        PX_I2C_CFG_SDA_SET_LO();
    }
    px_i2c_delay_half_clk();

    if(!px_i2c_scl_set_hiz_and_wait_clk_stretch())
    {
        // Error
        PX_I2C_CFG_SDA_SET_HIZ();
        return false;
    }

    // Check bit
    if(bit)
    {
        if(PX_I2C_CFG_SDA_IS_LO())
        {
            // Error
            return false;
        }
    }

    PX_I2C_CFG_SCL_SET_LO();

    return true;
}

static bool px_i2c_rx_bit(bool *bit)
{
    // Release data line so that slave can drive it
    PX_I2C_CFG_SDA_SET_HIZ();
    px_i2c_delay_half_clk();

    if(!px_i2c_scl_set_hiz_and_wait_clk_stretch())
    {
        // Timed out waiting for SCL to go high
        return false;
    }
    // Read bit
    *bit = PX_I2C_CFG_SDA_IS_HI();

    PX_I2C_CFG_SCL_SET_LO();

    return true;
}

static bool px_i2c_start(uint8_t sla_adr, px_i2c_start_t start)
{   
    // Check bus
    if(PX_I2C_CFG_SCL_IS_LO() || PX_I2C_CFG_SDA_IS_LO())
    {
        // Bus error
        return false;
    }

    // Generate START
    PX_I2C_CFG_SDA_SET_LO();
    px_i2c_delay_half_clk();
    PX_I2C_CFG_SCL_SET_LO();
    px_i2c_delay_half_clk();

    // Add R/W bit
    sla_adr = (sla_adr << 1) | start;
    // Send address
    return px_i2c_wr_u8(sla_adr);
}

static bool px_i2c_start_repeat(uint8_t sla_adr, px_i2c_start_t start)
{
    PX_I2C_CFG_SDA_SET_HIZ();
    px_i2c_delay_half_clk();

    // Wait for SCL to be high (check for clock stretching)
    if(!px_i2c_scl_set_hiz_and_wait_clk_stretch())
    {
        // Timed out waiting for SCL to be high
        return false;
    }

    return px_i2c_start(sla_adr, start);
}

static bool px_i2c_stop(void)
{
    PX_I2C_CFG_SDA_SET_LO();
    px_i2c_delay_half_clk();

    // Wait for SCL to be high (check for clock stretching)
    if(!px_i2c_scl_set_hiz_and_wait_clk_stretch())
    {
        // Timed out waiting for SCL to be high
        PX_I2C_CFG_SDA_SET_HIZ();
        return false;
    }

    // Generate STOP condition
    PX_I2C_CFG_SDA_SET_HIZ();
    px_i2c_delay_half_clk();

    if(PX_I2C_CFG_SDA_IS_LO())
    {
        // Error
        return false;
    }

    return true;
}

static bool px_i2c_wr_u8(uint8_t data)
{
    uint8_t bit_mask;
    bool    nak;

    // Send 8 bits, MSB first
    for(bit_mask = (1<<7); bit_mask != 0; bit_mask >>= 1)
    {
        // Send next bit
        if(!px_i2c_tx_bit((data & bit_mask) != 0))
        {
            // Error
            return false;
        }
    }
    // Receive ACK/NAK
    if(!px_i2c_rx_bit(&nak))
    {
        // Error
        return false;
    }
    // Did slave ACK byte written to it?
    if(nak)
    {
        // Release bus
        px_i2c_stop();            
        return false;
    }

    return true;
}

static bool px_i2c_rd_u8(uint8_t *data, bool nak)
{
    uint8_t bit_mask;
    bool    bit;
    uint8_t byte;

    byte = 0;
    for(bit_mask = (1<<7); bit_mask != 0; bit_mask >>= 1)
    {
        // Receive next bit
        if(!px_i2c_rx_bit(&bit))
        {
            // Error
            return false;
        }
        // Assemble byte from received bits
        if(bit)
        {
            byte |= bit_mask;
        }
    }
    // Return received byte
    *data = byte;

    // Send ACK/NAK
    if(!px_i2c_tx_bit(nak))
    {
        // Error

static void px_i2c_init_peripheral_data(px_i2c_per_t    peripheral,
                                        px_i2c_data_t * i2c_data)
{
    // Set peripheral
    i2c_data->peripheral = peripheral;
    // Clear reference counter
    i2c_data->open_counter = 0;
}

/* _____GLOBAL FUNCTIONS_____________________________________________________ */
void px_i2c_init(void)
{
    // Initialize peripheral data for each enabled peripheral
#if PX_I2C_CFG_I2C0_EN
    px_i2c_init_peripheral_data(PX_I2C_PER_0, &px_i2c0_data);
#endif

    // Set bus to free state (Hi-Z)
    PX_I2C_CFG_SDA_SET_HIZ();
    PX_I2C_CFG_SCL_SET_HIZ();
    px_i2c_delay_half_clk();
}

bool px_i2c_open(px_i2c_handle_t * handle,
                 px_i2c_per_t      peripheral,
                 uint8_t           slave_adr)
{
px_i2c_data_t * i2c_data;

    // Verify that pointer to handle is not NULL
    PX_DBG_ASSERT(handle != NULL);

    // Handle not initialised
    handle->i2c_data = NULL;

    // Set pointer to peripheral data
    switch(peripheral)
    {
#if PX_I2C_CFG_I2C0_EN
    case PX_I2C_PER_0:
        i2c_data = &px_i2c0_data;
        break;
#endif
    default:
        PX_DBG_ERR("Invalid peripheral specified");
        return false;
    }

    // Save 7-bit slave address
    handle->slave_adr = slave_adr;

    // Point handle to peripheral
    handle->i2c_data = i2c_data;

    // Success
    i2c_data->open_counter++;
    return true;
}

bool px_i2c_close(px_i2c_handle_t * handle)
{
    px_i2c_data_t * i2c_data;

    // Verify that pointer to handle is not NULL
    PX_DBG_ASSERT(handle != NULL);
    // Set pointer to peripheral
    i2c_data = handle->i2c_data;
    // Check that handle is open
    PX_DBG_ASSERT(i2c_data != NULL);
    PX_DBG_ASSERT(i2c_data->open_counter != 0);

    // Decrement open count
    i2c_data->open_counter--;

    // More open handles referencing peripheral?
    if(i2c_data->open_counter != 0)
    {
        // Close handle
        handle->i2c_data = NULL;
        // Success
        return true;
    }

   // Close handle
   handle->i2c_data = NULL;

    // Success
    return true;
}

bool px_i2c_wr(px_i2c_handle_t * handle,
               const void *      data,
               size_t            nr_of_bytes,
               uint8_t           flags)
{
    px_i2c_data_t * i2c_data;
    const uint8_t * data_u8 = (const uint8_t *)data;

    // Verify that pointer to handle is not NULL
    PX_DBG_ASSERT(handle != NULL);
    // Set pointer to peripheral
    i2c_data = handle->i2c_data;
    // Check that handle is open
    PX_DBG_ASSERT(i2c_data != NULL);
    PX_DBG_ASSERT(i2c_data->open_counter != 0);
    // Check that slave address is 7 bits
    PX_DBG_ASSERT(handle->slave_adr < 0x80);
#if !DBG
    // Supress compiler warning about unused variable
    (void)i2c_data;
#endif

    // Generate START or REPEATED START condition?
    if(  (flags & PX_I2C_FLAG_START) || (flags & PX_I2C_FLAG_REP_START)  )
    {
        if(flags & PX_I2C_FLAG_START)
        {
            if(!px_i2c_start(handle->slave_adr, PX_I2C_START_W))
            {
                return false;
            }
        }
        else
        {
            if(!px_i2c_start_repeat(handle->slave_adr, PX_I2C_START_W))
            {
                return false;
            }
        }
    }

    while(nr_of_bytes != 0)
    {
        // Send data
        if(!px_i2c_wr_u8(*data_u8))
        {
            // Error
            return false;
        }
        // Next byte
        data_u8++;
        nr_of_bytes--;        
    }

    // Generate STOP condition?
    if(flags & PX_I2C_FLAG_STOP)
    {
        if(!px_i2c_stop())
        {
            return false;
        }
    }

    // Success
    return true;
}

bool px_i2c_rd(px_i2c_handle_t * handle,
               void *            data,
               size_t            nr_of_bytes,
               uint8_t           flags)
{
    px_i2c_data_t * i2c_data;
    uint8_t *       data_u8 = (uint8_t *)data;

    // Verify that pointer to handle is not NULL
    PX_DBG_ASSERT(handle != NULL);
    // Set pointer to peripheral
    i2c_data = handle->i2c_data;
    // Check that handle is open
    PX_DBG_ASSERT(i2c_data != NULL);
    PX_DBG_ASSERT(i2c_data->open_counter != 0);
    // Check that slave address is 7 bits
    PX_DBG_ASSERT(handle->slave_adr < 0x80);
#if !DBG
    // Supress compiler warning about unused variable
    (void)i2c_data;
#endif

    // Generate START or REPEATED START condition?
    if(  (flags & PX_I2C_FLAG_START) || (flags & PX_I2C_FLAG_REP_START)  )
    {
        if(flags & PX_I2C_FLAG_START)
        {
            if(!px_i2c_start(handle->slave_adr, PX_I2C_START_W))
            {
                return false;
            }
        }
        else
        {
            if(!px_i2c_start_repeat(handle->slave_adr, PX_I2C_START_W))
            {
                return false;
            }
        }
    }

    while(nr_of_bytes != 0)
    {
        if((nr_of_bytes == 1) && (nak_last_byte))
        {
            if(!px_i2c_rd_u8(data_u8, true))
            {
                // Error
                return false;
            }
        }
        else
        {
            if(!px_i2c_rd_u8(data_u8, false))
            {
                // Error
                return false;
            }
        }
        
        // Next byte
        data_u8++;
        nr_of_bytes--;
    }

    // Generate STOP condition?
    if(flags & PX_I2C_FLAG_STOP)
    {
        if(!px_i2c_stop())
        {
            return false;
        }
    }

    // Success
    return true;
}

void px_i2c_change_slave_adr(px_i2c_handle_t * handle,
                             uint8_t           slave_adr)
{
    // Verify that pointer to handle is not NULL
    PX_DBG_ASSERT(handle != NULL);
    // Check that slave address is 7 bits
    PX_DBG_ASSERT(slave_adr < 0x80);

    handle->slave_adr = slave_adr;
}
