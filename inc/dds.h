/*
 * dds.h
 *
 *      Author: Jakub Janeczko <jjaneczk@gmail.com>
 */

#ifndef INC_DDS_H_
#define INC_DDS_H_

#include <stdbool.h>

#ifndef likely
 #define likely(x) (x)
#endif

#ifndef unlikely
 #define unlikely(x) (x)
#endif

/*#ifndef __packed
 #define __packed __attribute__((packed))
#endif*/

enum dds_data_format {
	DDS_FORMAT_8bit,
	DDS_FORMAT_12bit_LEFT,
	DDS_FORMAT_12bit_RIGHT,
};

enum dds_mode {
	DDS_MODE_INDEPENDENT,
	DDS_MODE_SINGLE_TRIGGER,
	DDS_MODE_DUAL,
};

typedef struct dds_struct {
	void (*dds_sync)(void);
	void (*dds_err)(void);
} dds;

typedef __packed struct dds_channel_config {
	uint8_t 		enabled;		/* channel enabled 						*/

	/* data */
	uint8_t			data_format;	/* samples data format 					*/
	uint32_t		data_offset;	/* offset from data field				*/
	uint32_t		data_size;		/* size of data 						*/

	uint32_t		period;			/* timer period */
	uint16_t		prescaler;		/* timer prescaler */

} dds_chconfig;

typedef __packed struct dds_header_struct {
	/* header */
	char 	 		magic[4];		/* "MARM" 								*/
	uint32_t 		checksum;   	/* frame checksum 						*/
	uint32_t 	 	size;			/* frame size 							*/

	/* configuration */
	uint8_t 		mode;			/* mode of operation 					*/

	dds_chconfig	ch[2];			/* DAC channel 1 and 2 					*/

	/* data */
	void  			*data[0];		/* samples	 							*/
} dds_header;

typedef enum dds_res {
	DDS_OK = 0,
	DDS_ERR_HEADER,
	DDS_ERR_CHECKSUM,
	DDS_ERR_DATA,
	DDS_ERR_CONFIG,
	DDS_ERR_MEM,
	DDS_ERR_TIMEOUT,
} dds_res;

const char *dds_res_to_str(enum dds_res res);

bool dds_verify_header(dds_header *header);

int DDS_Start(dds_header *header);

void DDS_Stop(void);

void DDS_Init(dds dds_struct);

#endif /* INC_DDS_H_ */
