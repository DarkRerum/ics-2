#include <systemc.h>
#include <slave_driver.h>
#include <set>

static bool stop_detected;
static bool start_detected;

static bool sda;
static bool release_sda;

typedef struct reg_t {
	sc_uint<8> addr;
	sc_uint<8> value;
} reg_t;

static reg_t registers[] = {
	{0x00, 0x01}, // temperature_value_msb
	{0x01, 0x90}, // temperature_value_lsb
	{0x02, 0x00}, // status
	{0x03, 0x00}, // configuration
	{0x04, 0x00}, // t_high_setpoint_msb
	{0x05, 0x00}, // t_high_setpoint_lsb
	{0x06, 0x00}, // t_low_setpoint_msb
	{0x07, 0x00}, // t_low_setpoint_lsb
	{0x08, 0x00}, // t_crit_setpoint_msb
	{0x09, 0x00}, // t_crit_setpoint_lsb
	{0x0A, 0x00}, // t_hyst_setpoint
	{0x0B, 0x00}, // id
	{0x2F, 0x00}  // software_reset
};

static reg_t & get_reg(sc_uint<8> address)
{
	for (size_t i = 0; i < ( sizeof(registers) / sizeof(registers[0]) ); i++) 
	{
		if (registers[i].addr == address)
		{
			return registers[i];
		}
	}
	return registers[0];
}

static sc_uint<8> address_pointer;

void slave_driver::emulate_slave()
{
	enum {IDLE, WAIT_SCL0_INIT, WAIT_SCL1, WAIT_SCL0};	
	enum {DEVICE_ADDRESS, INNER_ADDRESS, WDATA, RDATA};
	static const int MAX_DATA_INDEX = 7;
	static const int ACK_INDEX = 8;

	static int exec_state;
	static int data_type;
	static sc_uint<8> data_out;
	static int bit_index;
	static bool ack;

	static bool repeat_start;
	static bool read_not_write;
	
    if (!rstn_i.read())
    {
		read_not_write = false;
		repeat_start = false;
		exec_state = IDLE;
		sda = 1;
		release_sda = 1;
		ready_o.write(1);
		address_pointer = 0;
    }
    else
    {
		if (stop_detected)	
		{
			read_not_write = false;
			repeat_start = false;
			ready_o.write(1);
            exec_state = IDLE;
			release_sda = 1;
		}
		else 
		{
			switch (exec_state)
			{
			case (IDLE):
				release_sda = 1;
				if (start_detected)
				{
					exec_state = WAIT_SCL0_INIT;
					data_type = DEVICE_ADDRESS;
					ready_o.write(0);
					ack = 1;
				}
				break;

			case (WAIT_SCL0_INIT):
				if (i2c_scl_i.read() == '0') 
				{
					exec_state = WAIT_SCL1;
					bit_index = 0;
					data_out = 0;
				}
				break;

			case (WAIT_SCL1):
				if (i2c_scl_i.read() == '1')
				{
					release_sda = 1;
					exec_state = WAIT_SCL0;
					if (data_type != RDATA && bit_index != ACK_INDEX)
					{
						data_out[MAX_DATA_INDEX - bit_index] = (i2c_sda_io.read() == '0') ? 0 : 1;
					}
					if (data_type == RDATA && bit_index == ACK_INDEX)
					{
						ack = (i2c_sda_io.read() == '0') ? 0 : 1;
					}
					if (bit_index == ACK_INDEX)
					{
						bit_index = 0;
						data_out_bo.write(data_out);
					}
					else
					{
						bit_index++;
					}
				}
				break;

			case (WAIT_SCL0):
				if (start_detected)
				{
					exec_state = WAIT_SCL0_INIT;
					data_type = DEVICE_ADDRESS;
					ack = 1;
					repeat_start = true;
				}
				else if (i2c_scl_i.read() == '0') 
				{
					exec_state = WAIT_SCL1;
					if (bit_index == ACK_INDEX)
					{
						if (data_type == INNER_ADDRESS)
						{
							address_pointer = data_out;
						}
						else if (data_type == WDATA)
						{
							get_reg(address_pointer).value = data_out;
						}
						if ( (data_type == RDATA || data_type == WDATA) && bit_index == ACK_INDEX)
						{
							address_pointer++;
						}
						release_sda = (data_type == RDATA) ? 1 : 0;
						if (data_type != RDATA)
						{
							sda = 0;
						}
						if (data_type == DEVICE_ADDRESS)
						{
							if (repeat_start)
							{
								data_type = (data_out[0] == 1) ? RDATA : WDATA;
							}
							else
							{
								read_not_write = data_out[0];
								data_type = INNER_ADDRESS;
							}
						}
						else if (data_type == INNER_ADDRESS)
						{
							data_type = (read_not_write) ? RDATA : WDATA;
						}
					}
					else
					{
						if (data_type == RDATA && ack == 1)
						{
							release_sda = 1;
						}
						else
						{
							release_sda = (data_type == RDATA) ? 0 : 1;
						}
						if (data_type == RDATA)
						{
							sda = get_reg(address_pointer).value[MAX_DATA_INDEX - bit_index];
						}
					}
				}
				break;
			}
		}
    }
}


void slave_driver::listen_start()
{
	enum {WAIT_SCL1, WAIT_SDA1, WAIT_SDA0};

	static int start_state;
	
	if (!rstn_i.read())
	{
		start_state = WAIT_SCL1;
		start_detected = false;
	}
	else 
	{
		start_detected = false;
		switch (start_state) {
		    case WAIT_SCL1:
		        if (i2c_scl_i.read() == 1 && i2c_sda_io.read() == 0) start_state = WAIT_SDA1;
		        if (i2c_scl_i.read() == 1 && i2c_sda_io.read() == 1) start_state = WAIT_SDA0;
		    	break;

		    case WAIT_SDA1:
		        if (i2c_scl_i.read() == 0) start_state = WAIT_SCL1;
		        if (i2c_scl_i.read() == 1 && i2c_sda_io.read() == 1) start_state = WAIT_SDA0;
				break;

		    case WAIT_SDA0:
				if (i2c_scl_i.read() == 0)
				{
					start_state = WAIT_SCL1;
				}
		        if (i2c_scl_i.read() == 1 && i2c_sda_io.read() == 0)
				{
		            start_detected = 1;
		            start_state = WAIT_SCL1;
				}
		        break;

		    default:
		        start_state = WAIT_SCL1;
				break;
		}
	}
}

void slave_driver::listen_stop()
{
	enum {WAIT_SCL1, WAIT_SDA1, WAIT_SDA0};

	static int stop_state;	

	if (!rstn_i.read())
	{
		stop_detected = 0;
		stop_state = WAIT_SCL1;
	}
    else
	{
		stop_detected = 0;
            
        switch (stop_state)
		{
            case WAIT_SCL1:
                if (i2c_scl_i.read() == 1 && i2c_sda_io.read() == 0) stop_state = WAIT_SDA1;
				break;

            case WAIT_SDA1:
                if (i2c_scl_i.read() == 1 && i2c_sda_io.read() == 1)
				{
                    stop_detected = 1;
                    stop_state = WAIT_SCL1;
                }
                else if (i2c_scl_i.read() == 0)
				{
                    stop_state = WAIT_SCL1;
                }
				break;

            default:
                stop_state = WAIT_SCL1;
				break;
        }
	}
}

void slave_driver::output_selector()
{
    if (!rstn_i.read())
    {	
        //ready_o.write(true);
    }
    else
    {
		if (!release_sda) 
		{
			i2c_sda_io.write(sc_logic(sda));
		}
		else
		{
			//i2c_sda_io.write(sc_logic('z'));
		}
    }
}
