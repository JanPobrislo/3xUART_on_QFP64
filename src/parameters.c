#include "em_msc.h"
#include <string.h>
#include <stdio.h>
//#include <stdint.h>
//#include <stdbool.h>
#include "parameters.h"
#include "uart1.h"
#include "rtc.h"

tci_parameters param;

routes_for_tx tx_route;  // definuje routu pro vysilani

unsigned long uptime;    // cas od resetu v sec

//------------------------------------------------------------------------------
// Docasne globalni parametry - Prepinace pro zobrazovani na consoli COM-B UART1
//------------------------------------------------------------------------------
unsigned char show_timetick = 0;  // povoluje zobrazovani . kazdou sec
unsigned char show_inputs = 0;    // povoluje zobrazovani vstupu kazdou sec
unsigned char show_gps_nmea = 0;  // povoluje zobrazovani . kazdou sec
unsigned char show_rxtx_details = 1;

//------------------------------------------------------------------------------
// Statistika prijmu
//------------------------------------------------------------------------------
typedef struct {
	unsigned char net;
	unsigned char dau;
	unsigned long count;
} type_rx_statistic;

#define MAX_STATISTICAL_RECORDS 30

typedef struct {
    rtc_datetime_t init_time;
	type_rx_statistic rx_count[MAX_STATISTICAL_RECORDS];
} type_statistical;

type_statistical stat;

void clear_statistics(void)
{
	unsigned char n;
	get_rtc(&stat.init_time);
	for (n=0; n<MAX_STATISTICAL_RECORDS; n++) {
		stat.rx_count[n].net=0;
		stat.rx_count[n].dau=0;
		stat.rx_count[n].count=0;
	}
/*
	stat.rx_count[0].net=15;
	stat.rx_count[0].dau=3;
	stat.rx_count[0].count=108;

	stat.rx_count[1].net=15;
	stat.rx_count[1].dau=2;
	stat.rx_count[1].count=77108;
*/
}

//-- Pokud neni nastavene datum (2000-00-00) tak maze a inicializuje statistiku
void init_statistics(void)
{
	if (stat.init_time.year==0 && stat.init_time.month==0 && stat.init_time.day==0) {
		clear_statistics();
	}
}

void show_statistics(void)
{
	unsigned char n;
    char buf[300];

	sendStringUART1("\r\n------------------------\r\n");
	sendStringUART1("  RX statistics from\r\n  ");
	sprintf(buf, "20%02u-%02u-%02u %02u:%02u:%02u\r\n",
            stat.init_time.year, stat.init_time.month, stat.init_time.day,
            stat.init_time.hour, stat.init_time.min,   stat.init_time.sec);
    sendStringUART1(buf);
	sendStringUART1("------------------------\r\n");

    sendStringUART1("  NET-DAU  RX COUNT\r\n");

	for (n=0; n<MAX_STATISTICAL_RECORDS; n++) {
		if (stat.rx_count[n].net==0) break;

		sprintf(buf,"   %02u-%02u : %lu\r\n",
			stat.rx_count[n].net,
			stat.rx_count[n].dau,
			stat.rx_count[n].count);
	    sendStringUART1(buf);
	}
	sendStringUART1("------------------------\r\n");
}

void insert_statistics(unsigned char RXnet, unsigned char RXdau)
{
	unsigned char n;
	for (n=0; n<MAX_STATISTICAL_RECORDS; n++)
	{
		if (stat.rx_count[n].net==RXnet && stat.rx_count[n].dau==RXdau ) {
			stat.rx_count[n].count++;
			break;
		}

		if (stat.rx_count[n].net==0) {
			stat.rx_count[n].net=RXnet;
			stat.rx_count[n].dau=RXdau;
			stat.rx_count[n].count=1;
			break;
		}
	}
}
//---------------- --------------------------------------------------------------
// Ulozeni parametru do FLASH (USERDATA stranka)
// USERDATA_BASE = 0x0FE00000, velikost 2048 B
// Vraci: 0=OK, 1=chyba mazani, 2=chyba zapisu
//
// Format v USERDATA:
//   [0..3]   magic number 0x54434900 ('TCI\0')
//   [4..7]   velikost struktury (kontrola kompatibility)
//   [8..]    data struktury tci_parameters
//------------------------------------------------------------------------------
#define PARAM_MAGIC      0x54434900UL   // 'TCI\0'
#define PARAM_FLASH_ADDR ((uint32_t *)USERDATA_BASE)

unsigned char parameters_save(void)
{
    MSC_Status_TypeDef ret;

    // Sestavit buffer zarovnany na 4 byty
    // Header: magic(4) + size(4) + data
    uint32_t dataSize = sizeof(tci_parameters);
    uint32_t bufSize  = 8 + dataSize;

    // Zaokrouhlit na nasobek 4 (pozadavek MSC_WriteWord)
    if (bufSize % 4 != 0) bufSize += 4 - (bufSize % 4);

    uint8_t buf[bufSize];
    memset(buf, 0xFF, bufSize);

    // Zapsat header
    ((uint32_t *)buf)[0] = PARAM_MAGIC;
    ((uint32_t *)buf)[1] = dataSize;

    // Zapsat data
    memcpy(buf + 8, &param, sizeof(tci_parameters));

    // Inicializovat MSC
    MSC_Init();

    // Smazat stranku
    ret = MSC_ErasePage(PARAM_FLASH_ADDR);
    if (ret != mscReturnOk) {
        MSC_Deinit();
        sendStringUART1("FLASH: erase error\r\n");
        return 1;
    }

    // Zapsat data
    ret = MSC_WriteWord(PARAM_FLASH_ADDR, buf, bufSize);
    if (ret != mscReturnOk) {
        MSC_Deinit();
        sendStringUART1("FLASH: write error\r\n");
        return 2;
    }

    MSC_Deinit();
    sendStringUART1("FLASH: parameters saved\r\n");
    return 0;
}

//------------------------------------------------------------------------------
// Nacteni parametru z FLASH (USERDATA stranka)
// Vraci: 0=OK, 1=neplatna data (magic), 2=nekompatibilni velikost
// Pokud nacteni selze, param zustane beze zmeny
//------------------------------------------------------------------------------
unsigned char parameters_load(void)
{
    uint32_t *flash = PARAM_FLASH_ADDR;

    // Zkontrolovat magic number
    if (flash[0] != PARAM_MAGIC) {
        sendStringUART1("FLASH: no valid data\r\n");
        return 1;
    }

    // Zkontrolovat velikost struktury
    uint32_t savedSize = flash[1];
    if (savedSize != sizeof(tci_parameters)) {
        sendStringUART1("FLASH: size mismatch, using defaults\r\n");
        return 2;
    }

    // Nacist data
    memcpy(&param, (uint8_t *)flash + 8, sizeof(tci_parameters));

    sendStringUART1("FLASH: parameters loaded\r\n");
    return 0;
}

void parameters_init(void) {
	unsigned char n;

	param.primary_net = 15;
	param.next_time = 3;
	param.next_rpt = 1;
	param.error_rpt = 2;
	param.pretime = 10;
	param.deadtime = 11;
	param.sys_tok = 12;
	param.out_enabled = 1;

	for (n=0; n<MAX_NETS; n++) {
		param.netdau[n] = 0;
	}

	for (n=0; n<MAX_ROUTES; n++) {
		param.route[n].net = 0;
		param.route[n].path = 0;
		param.route[n].dau = 0;
		param.route[n].follow = 0;
		param.route[n].error = 0;
		param.route[n].revers = 0;
	}

	//---- Default pro ladeni
	param.netdau[14] = 3;

	param.route[0].net = 15;
	param.route[0].path = 255;
	param.route[0].dau = 255;
	param.route[0].follow = 4;
	param.route[0].error = 2;
	param.route[0].revers = 2;

	param.route[1].net = 15;
	param.route[1].path = 255;
	param.route[1].dau = 2;
	param.route[1].follow = 8;
	param.route[1].error = 2;
	param.route[1].revers = 2;

	param.route[2].net = 15;
	param.route[2].path = 15;
	param.route[2].dau = 255;
	param.route[2].follow = 2;
	param.route[2].error = 9;
	param.route[2].revers = 2;

	param.route[3].net = 15;
	param.route[3].path = 0;
	param.route[3].dau = 2;
	param.route[3].follow = 4;
	param.route[3].error = 4;
	param.route[3].revers = 2;
}

void parameters_clear_netdau(void) {
	unsigned char n;
	for (n=0; n<MAX_NETS; n++) {
		param.netdau[n] = 0;
	}
}

void parameters_clear_routes(void) {
	unsigned char n;
	for (n=0; n<MAX_ROUTES; n++) {
		param.route[n].net = 0;
		param.route[n].path = 0;
		param.route[n].dau = 0;
		param.route[n].follow = 0;
		param.route[n].error = 0;
		param.route[n].revers = 0;
	}
}

void parameters_show(void) {
	unsigned char n;
	char txt[250] = "";

	sendStringUART1("\r\n-------------------------------------------------\r\n");
	sendStringUART1("   TCI-S ");
	sendStringUART1(PROGRAM_VERSION);
	sendStringUART1("   P A R A M E T E R S\r\n");
	sendStringUART1("-------------------------------------------------\r\n");
	sprintf(txt,"PRIMARY NET: %u\r\n",param.primary_net);
	sendStringUART1(txt);
	sprintf(txt,"NEXT TIME: %u\r\n",param.next_time);
	sendStringUART1(txt);
	sprintf(txt,"NEXT RPT : %u\r\n",param.next_rpt);
	sendStringUART1(txt);
	sprintf(txt,"ERROR RPT: %u\r\n",param.error_rpt);
	sendStringUART1(txt);
	sprintf(txt,"PRETIME  : %u\r\n",param.pretime);
	sendStringUART1(txt);
	sprintf(txt,"DEADTIME : %u\r\n",param.deadtime);
	sendStringUART1(txt);
	sprintf(txt,"SYS.TOK  : %u\r\n",param.sys_tok);
	sendStringUART1(txt);
	sprintf(txt,"OUT ENAB.: %u\r\n",param.out_enabled);
	sendStringUART1(txt);

	sendStringUART1("-------------------------------------------------\r\nNET:");
	for (n=0; n<MAX_NETS; n++) {
		sprintf(txt," %02u",n+1);
		sendStringUART1(txt);
	}
	sendStringUART1("\r\nDAU:");
	for (n=0; n<MAX_NETS; n++) {
		if (param.netdau[n]!=0) {
			sprintf(txt," %02u",param.netdau[n]);
			sendStringUART1(txt);
		}
		else {
			sendStringUART1(" x ");
		}
	}
	sendStringUART1("\r\n-------------------------------------------------");

	sendStringUART1("\r\nROUTE: NET PTH DAU -> FLW ERR REV\r\n");
//	for (n=0; n<MAX_ROUTES; n++) {
	n=0;
//	while ((n<MAX_ROUTES)&&(param.route[n].path!=0)) {
	for (n=0; n<MAX_ROUTES; n++) {
		if (param.route[n].net != 0) {
			sprintf(txt,"       %02u",param.route[n].net);
			sendStringUART1(txt);
			if (param.route[n].path==255) {
				sendStringUART1("  * ");
			}
			else {
				sprintf(txt,"  %02u",param.route[n].path);
				sendStringUART1(txt);
			}
			if (param.route[n].dau==255) {
				sendStringUART1("  * ");
			}
			else {
				sprintf(txt,"  %02u",param.route[n].dau);
				sendStringUART1(txt);
			}
			sprintf(txt,"  -> %02u",param.route[n].follow);
			sendStringUART1(txt);
			sprintf(txt,"  %02u",param.route[n].error);
			sendStringUART1(txt);
			sprintf(txt,"  %02u\r\n",param.route[n].revers);
			sendStringUART1(txt);
		}
	}
//	sendStringUART1("       -------------------------\r\n");
	sendStringUART1("-------------------------------------------------\r\n");
	sprintf(txt,"UPTIME: %lu sec    ",uptime);
	sendStringUART1(txt);
	show_rtc();
	sendStringUART1("-------------------------------------------------\r\n");
}

//------------------------------------------------------------------------------
// Podle parametru NET,PATH,DAU nacte z route table a nastavi promenou tx_route
//------------------------------------------------------------------------------
/*	tx_route.follow = 4;
	tx_route.error  = 4;
	tx_route.revers = 2;
*/
unsigned char make_tx_route(unsigned char net, unsigned char path, unsigned char dau)
{
    unsigned char p;
    unsigned char d;
    // Index nalezeného záznamu pro každou prioritu, -1 = nenalezen
    int found[4] = { -1, -1, -1, -1 };

    for (int n = 0; n < MAX_ROUTES; n++) {

        if (param.route[n].net == net) {  // net se musí vždy shodovat

			p = param.route[n].path;
			d = param.route[n].dau;

			if (p == 255 && d == 255) {
				if (found[0] == -1) found[0] = n;  // priorita 1: net,*,*
			}
			else if (p == 255 && d == dau) {
				if (found[1] == -1) found[1] = n;  // priorita 2: net,*,dau
			}
			else if (p == path && d == 255) {
				if (found[2] == -1) found[2] = n;  // priorita 3: net,path,*
			}
			else if (p == path && d == dau) {
				if (found[3] == -1) found[3] = n;  // priorita 4: net,path,dau
			}
        }
    }

    // Vybrat záznam s nejvyšší prioritou (4 > 3 > 2 > 1)
    int best = -1;
    for (int pri = 3; pri >= 0; pri--) {
        if (found[pri] != -1) {
            best = found[pri];
            break;
        }
    }

    if (best == -1) {
        tx_route.follow = 0;
        tx_route.error  = 0;
        tx_route.revers = 0;
        return 1;  // chyba: žádný záznam nenalezen
    }

    tx_route.follow = param.route[best].follow;
    tx_route.error  = param.route[best].error;
    tx_route.revers = param.route[best].revers;
    return 0;  // OK
}
