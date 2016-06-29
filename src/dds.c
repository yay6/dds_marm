/*
 * dds.c
 *
 *      Author: Jakub Janeczko <jjaneczk@gmail.com>
 */

#include <stddef.h>
#include <stdbool.h>

#include "stm32f4xx_dac.h"
#include "stm32f4xx_dma.h"
#include "stm32f4xx_gpio.h"

#include "dds.h"

static struct dds_struct state;

void DMA1_Stream5_IRQHandler(void)
{
	if (DMA_GetITStatus(DMA1_Stream5, DMA_IT_TCIF5) == SET) {
		DMA_ClearITPendingBit(DMA1_Stream5, DMA_IT_TCIF5);

		// Transfer complete interrupt
		if (likely(state.dds_sync))
			state.dds_sync();
	}
	if (DMA_GetITStatus(DMA1_Stream5, DMA_IT_DMEIF5) == SET) {
		DMA_ClearITPendingBit(DMA1_Stream5, DMA_IT_DMEIF5);

		// Direct mode error interrupt
		if (likely(state.dds_err))
			state.dds_err();
	}
	if (DMA_GetITStatus(DMA1_Stream5, DMA_IT_FEIF5) == SET) {
		DMA_ClearITPendingBit(DMA1_Stream5, DMA_IT_FEIF5);

		// FIFO error interrupt
		if (likely(state.dds_err))
			state.dds_err();
	}
}

void TIM6_DAC_IRQHandler(void)
{
	if (DAC_GetITStatus(DAC_Channel_1, DAC_IT_DMAUDR) == SET) {
		DAC_ClearITPendingBit(DAC_Channel_1, DAC_IT_DMAUDR);

		// DMA underrun interrupt
		if (likely(state.dds_err))
			state.dds_err();
	}
	if (DAC_GetITStatus(DAC_Channel_2, DAC_IT_DMAUDR) == SET) {
		DAC_ClearITPendingBit(DAC_Channel_2, DAC_IT_DMAUDR);

		// DMA underrun interrupt
		if (likely(state.dds_err))
			state.dds_err();
	}
}

static const char *dds_res_str[] = {
	"OK",
	"invalid header",
	"invalid checksum",
	"invalid data",
	"invalid configuration",
	"no enough memory",
	"timeout",
};

const char *dds_res_to_str(enum dds_res res)
{
	return dds_res_str[res];
}

bool dds_verify_header(dds_header *header)
{
	return  header->magic[0] == 'M' &&
			header->magic[1] == 'A' &&
			header->magic[2] == 'R' &&
			header->magic[3] == 'M';
}

bool dds_verify_checksum(dds_header *header)
{
	// TODO: verify checksum

	return true;
}

bool dds_verify_data(dds_header *header)
{
	// TODO verify data

	return true;
}

static void dds_gpio_init(void)
{
	GPIO_InitTypeDef gpio_init;

	/* PA4, PA5 - DAC outputs */
	gpio_init.GPIO_Pin  = GPIO_Pin_4 | GPIO_Pin_5;
	gpio_init.GPIO_Mode = GPIO_Mode_AN;
	gpio_init.GPIO_PuPd = GPIO_PuPd_NOPULL;

	GPIO_Init(GPIOA, &gpio_init);

	/* PA0 - synchronization */
	gpio_init.GPIO_Pin   = GPIO_Pin_0;
	gpio_init.GPIO_Mode  = GPIO_Mode_OUT;
	gpio_init.GPIO_OType = GPIO_OType_PP;
	gpio_init.GPIO_PuPd  = GPIO_PuPd_NOPULL;
	gpio_init.GPIO_Speed = GPIO_Speed_100MHz;

	GPIO_Init(GPIOA, &gpio_init);
}

static void dds_nvic_init(void)
{
	NVIC_InitTypeDef      nvic_init;

	nvic_init.NVIC_IRQChannel = DMA1_Stream5_IRQn;
	nvic_init.NVIC_IRQChannelPreemptionPriority = 0;
	nvic_init.NVIC_IRQChannelSubPriority = 1;
	nvic_init.NVIC_IRQChannelCmd = ENABLE;

	NVIC_Init(&nvic_init);
}

/* DHR registers offsets - copied from stm32f4xx_dac.c */
#define DHR12R1_OFFSET             ((uint32_t)0x00000008)
#define DHR12R2_OFFSET             ((uint32_t)0x00000014)
#define DHR12RD_OFFSET             ((uint32_t)0x00000020)

static void* dds_compute_dac_hdr_addr(int type, enum dds_data_format format)
{
	uint32_t addr = DAC_BASE;

	switch (type) {
	case 1:
		addr += DHR12R1_OFFSET;
		break;
	case 2:
		addr += DHR12R2_OFFSET;
		break;
	case 3:
		addr += DHR12RD_OFFSET;
		break;
	default:
		return NULL;
	}

	switch (format) {
	case DDS_FORMAT_8bit:
		addr += DAC_Align_8b_R;
		break;
	case DDS_FORMAT_12bit_LEFT:
		addr += DAC_Align_12b_L;
		break;
	case DDS_FORMAT_12bit_RIGHT:
		addr += DAC_Align_12b_R;
		break;
	}

	return (void*) addr;
}

#undef DHR12R1_OFFSET
#undef DHR12R2_OFFSET
#undef DHR12RD_OFFSET

void DDS_Init(dds dds_struct)
{
	state = dds_struct;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM7, ENABLE);

	dds_gpio_init();

	dds_nvic_init();
}

void DDS_Stop(void)
{
	DAC_DMACmd(DAC_Channel_1, DISABLE);
	DAC_DMACmd(DAC_Channel_2, DISABLE);

	DMA_DeInit(DMA1_Stream5);
	DMA_DeInit(DMA1_Stream6);

	DAC_Cmd(DAC_Channel_1, DISABLE);
	DAC_Cmd(DAC_Channel_2, DISABLE);

	TIM_DeInit(TIM6);
	TIM_DeInit(TIM7);

	TIM_Cmd(TIM6, DISABLE);
	TIM_Cmd(TIM7, DISABLE);
}

static void dds_dac_config(uint32_t DAC_Channel, uint32_t DAC_Trigger)
{
	DAC_InitTypeDef dac_init;

	DAC_StructInit(&dac_init);

	dac_init.DAC_Trigger = DAC_Trigger;

	DAC_Init(DAC_Channel, &dac_init);
}

static void dds_tim_config(TIM_TypeDef *TIMx, dds_chconfig *chconfig)
{
	TIM_TimeBaseInitTypeDef tim_init;

	TIM_TimeBaseStructInit(&tim_init);

	tim_init.TIM_Period        = chconfig->period;
	tim_init.TIM_Prescaler     = chconfig->prescaler;
	tim_init.TIM_ClockDivision = 0;
	tim_init.TIM_CounterMode   = TIM_CounterMode_Up;

	TIM_TimeBaseInit(TIMx, &tim_init);
	TIM_SelectOutputTrigger(TIMx, TIM_TRGOSource_Update);
}

static void dds_dma_config(DMA_Stream_TypeDef *DMAy_Streamx,
					 	   uint32_t DMA_Channel,
						   dds_header *header,
						   dds_chconfig *chconfig,
						   void *dds_dhr_addr)
{
	DMA_InitTypeDef dma_init;
	uint32_t periphDataSize;
	uint32_t memDataSize;

	DMA_DeInit(DMAy_Streamx);

	dma_init.DMA_Channel            = DMA_Channel;
	dma_init.DMA_PeripheralBaseAddr = (uint32_t) dds_dhr_addr;
	dma_init.DMA_Memory0BaseAddr    = ((uint32_t) header->data) + chconfig->data_offset;
	dma_init.DMA_DIR                = DMA_DIR_MemoryToPeripheral;
	dma_init.DMA_BufferSize         = chconfig->data_size;
	dma_init.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
	dma_init.DMA_MemoryInc          = DMA_MemoryInc_Enable;

	// set peripherial and memory data size
	if (header->mode == DDS_MODE_DUAL) {
		if (chconfig->data_format == DDS_FORMAT_8bit) {
			periphDataSize 	= DMA_PeripheralDataSize_HalfWord;
			memDataSize		= DMA_MemoryDataSize_HalfWord;
		} else {
			periphDataSize 	= DMA_PeripheralDataSize_Word;
			memDataSize		= DMA_MemoryDataSize_Word;
		}
	} else {
		if (chconfig->data_format == DDS_FORMAT_8bit) {
			periphDataSize 	= DMA_PeripheralDataSize_Byte;
			memDataSize		= DMA_MemoryDataSize_Byte;
		} else {
			periphDataSize 	= DMA_PeripheralDataSize_HalfWord;
			memDataSize		= DMA_MemoryDataSize_HalfWord;
		}
	}

	dma_init.DMA_PeripheralDataSize = periphDataSize;
	dma_init.DMA_MemoryDataSize     = memDataSize;

	dma_init.DMA_Mode               = DMA_Mode_Circular;
	dma_init.DMA_Priority           = DMA_Priority_High;
	dma_init.DMA_FIFOMode           = DMA_FIFOMode_Disable;
	dma_init.DMA_FIFOThreshold      = DMA_FIFOThreshold_HalfFull;
	dma_init.DMA_MemoryBurst        = DMA_MemoryBurst_Single;
	dma_init.DMA_PeripheralBurst    = DMA_PeripheralBurst_Single;

	DMA_Init(DMAy_Streamx, &dma_init);
}

static dds_res dds_run_independent(dds_header *header)
{
	void *hdr_addr;

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);

	// DAC channel1
	if (header->ch[0].enabled) {
		dds_chconfig *chc = &header->ch[0];

		dds_dac_config(DAC_Channel_1, DAC_Trigger_T6_TRGO);

		// TIM6
		RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);
		dds_tim_config(TIM6, chc);
		TIM_Cmd(TIM6, ENABLE);

		hdr_addr = dds_compute_dac_hdr_addr(1, chc->data_format);
		dds_dma_config(DMA1_Stream5, DMA_Channel_7, header, chc, hdr_addr);
		DAC_DMACmd(DAC_Channel_1, ENABLE);
	}

	// DAC channel2
	if (header->ch[1].enabled) {
		dds_chconfig *chc = &header->ch[0];

		dds_dac_config(DAC_Channel_2, DAC_Trigger_T7_TRGO);

		// TIM7
		RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM7, ENABLE);
		dds_tim_config(TIM7, chc);
		TIM_Cmd(TIM7, ENABLE);

		hdr_addr = dds_compute_dac_hdr_addr(2, chc->data_format);
		dds_dma_config(DMA1_Stream6, DMA_Channel_7, header, chc, hdr_addr);
		DAC_DMACmd(DAC_Channel_2, ENABLE);
	}

	return DDS_OK;
}

static dds_res dds_run_single_trigger(dds_header *header)
{
	void *hdr_addr;
	bool trigger_configured = false;

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);

	// DAC channel 1
	if (header->ch[0].enabled) {
		dds_chconfig *chc = &header->ch[0];

		dds_dac_config(DAC_Channel_1, DAC_Trigger_T6_TRGO);

		// TIM6
		RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);
		dds_tim_config(TIM6, &header->ch[0]);
		TIM_Cmd(TIM6, ENABLE);
		trigger_configured = true;

		hdr_addr = dds_compute_dac_hdr_addr(1, chc->data_format);
		dds_dma_config(DMA1_Stream5, DMA_Channel_7, header, chc, hdr_addr);
		DAC_DMACmd(DAC_Channel_1, ENABLE);
	}

	if (header->ch[1].enabled) {
		dds_chconfig *chc = &header->ch[1];

		dds_dac_config(DAC_Channel_2, DAC_Trigger_T6_TRGO);
		if (!trigger_configured) {
			// TIM6
			RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);
			dds_tim_config(TIM6, &header->ch[1]);
			TIM_Cmd(TIM6, ENABLE);
		}
		hdr_addr = dds_compute_dac_hdr_addr(2, chc->data_format);
		dds_dma_config(DMA1_Stream6, DMA_Channel_7, header, chc, hdr_addr);
		DAC_DMACmd(DAC_Channel_2, ENABLE);
	}

	return DDS_OK;
}

static dds_res dds_run_dual(dds_header *header)
{
	void *hdr_addr;

	if (unlikely(!header->ch[0].enabled && header->ch[1].enabled))
		return DDS_ERR_CONFIG;

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);

	dds_dac_config(DAC_Channel_1, DAC_Trigger_T6_TRGO);
	dds_dac_config(DAC_Channel_2, DAC_Trigger_T6_TRGO);

	// TIM6
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);
	dds_tim_config(TIM6, &header->ch[0]);
	TIM_Cmd(TIM6, ENABLE);

	hdr_addr = dds_compute_dac_hdr_addr(3, header->ch[0].data_format);
	dds_dma_config(DMA1_Stream5, DMA_Channel_7, header, &header->ch[0], hdr_addr);
	DAC_DMACmd(DAC_Channel_1, ENABLE);

	DMA_ITConfig(DMA1_Stream5, DMA_IT_TC, ENABLE);

	return DDS_OK;
}

int DDS_Start(dds_header *header)
{
	dds_res res;

	if (unlikely(!dds_verify_checksum(header)))
		return DDS_ERR_CHECKSUM;

	if (unlikely(!dds_verify_data(header)))
		return DDS_ERR_DATA;

	if (!header->ch[0].enabled && !header->ch[1].enabled) {
		DDS_Stop();
		return DDS_OK;
	}

	switch (header->mode) {
	case DDS_MODE_INDEPENDENT:
		res = dds_run_independent(header);
		break;
	case DDS_MODE_SINGLE_TRIGGER:
		res = dds_run_single_trigger(header);
		break;
	case DDS_MODE_DUAL:
		res = dds_run_dual(header);
		break;
	}

	if (unlikely(res != DDS_OK)) {
		DDS_Stop();
		return res;
	}

	return DDS_OK;
}
