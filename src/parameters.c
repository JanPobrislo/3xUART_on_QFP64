#include "em_msc.h"
#include <stdlib.h>
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

#define MAX_STATISTICS_RECORDS 30

typedef struct {
    rtc_datetime_t init_time;
	type_rx_statistic rx_count[MAX_STATISTICS_RECORDS];
	unsigned long error_count;
} type_statistical;

type_statistical stat;

void clear_statistic(void)
{
	unsigned char n;
	get_rtc(&stat.init_time);
	for (n=0; n<MAX_STATISTICS_RECORDS; n++) {
		stat.rx_count[n].net=0;
		stat.rx_count[n].dau=0;
		stat.rx_count[n].count=0;
	}
	stat.error_count = 0;
}

void init_statistic(void)
{
	switch (statistic_load()) {

		case 0: //-- nacetl OK
			break;

		case 1: //-- ve flash neni jeste ulozeno (prvni reset)
			clear_statistic();
			statistic_save();
			break;

		case 2: //-- ve flash je blbost
			if (stat.init_time.year==0 && stat.init_time.month==0 && stat.init_time.day==0) {
				//-- Pokud neni nastavene datum (2000-00-00) tak maze a inicializuje statistiku
				clear_statistic();
			}
			statistic_save();
			break;
	}
}

/*  //------------------ Nesetrizena varianta
void show_statistic(void)
{
	unsigned char n;
    char buf[300];
	unsigned long sum;

	sendStringUART1("\r\n---------------------\r\n");
	sendStringUART1("  RX STATISTIC FROM\r\n  ");
	sprintf(buf, "%02u-%02u-%02u %02u:%02u:%02u\r\n",
            stat.init_time.year, stat.init_time.month, stat.init_time.day,
            stat.init_time.hour, stat.init_time.min,   stat.init_time.sec);
    sendStringUART1(buf);
	sendStringUART1("---------------------\r\n");

    sendStringUART1("  NET-DAU  RX-COUNT\r\n");

    sum=0;
	for (n=0; n<MAX_STATISTICS_RECORDS; n++) {
		if (stat.rx_count[n].net==0) break;

		sprintf(buf,"   %02u-%02u : %lu\r\n",
			stat.rx_count[n].net,
			stat.rx_count[n].dau,
			stat.rx_count[n].count);
	    sendStringUART1(buf);
	    sum += stat.rx_count[n].count;
	}
	sendStringUART1("---------------------\r\n");
	sprintf(buf,"TOTAL OK : %lu\r\n",sum);
    sendStringUART1(buf);
	sprintf(buf,"  ERRORS : %lu\r\n",stat.error_count);
    sendStringUART1(buf);
	sendStringUART1("---------------------\r\n");
}
*/

// Pomocná funkce pro qsort - porovná dva záznamy sestupně podle count
static int compare_statistics(const void *a, const void *b) {
    type_rx_statistic *statA = (type_rx_statistic *)a;
    type_rx_statistic *statB = (type_rx_statistic *)b;

    if (statA->count < statB->count) return 1;
    if (statA->count > statB->count) return -1;
    return 0;
}

void show_statistic(void)
{
    unsigned char n;
    char buf[300];
    unsigned long sum;

    // Lokální kopie pole pro seřazení, abychom nezměnili data v globální proměnné 'stat'
    type_rx_statistic sorted_records[MAX_STATISTICS_RECORDS];
    memcpy(sorted_records, stat.rx_count, sizeof(sorted_records));

    // Seřadíme záznamy pomocí qsort (sestupně)
    qsort(sorted_records, MAX_STATISTICS_RECORDS, sizeof(type_rx_statistic), compare_statistics);

    sendStringUART1("\r\n---------------------\r\n");
    sendStringUART1("  RX STATISTIC FROM\r\n  ");
    sprintf(buf, "%02u-%02u-%02u %02u:%02u:%02u\r\n",
            stat.init_time.year, stat.init_time.month, stat.init_time.day,
            stat.init_time.hour, stat.init_time.min,   stat.init_time.sec);
    sendStringUART1(buf);
    sendStringUART1("---------------------\r\n");

    sendStringUART1("  NET-DAU  RX-COUNT\r\n");

    sum = 0;
    for (n = 0; n < MAX_STATISTICS_RECORDS; n++) {
        // Kontrolujeme seřazené záznamy. Pokud narazíme na count 0,
        // víme, že zbytek seřazeného pole už jsou prázdné záznamy.
        if (sorted_records[n].count == 0) continue;

        sprintf(buf, "   %02u-%02u : %lu\r\n",
                sorted_records[n].net,
                sorted_records[n].dau,
                sorted_records[n].count);
        sendStringUART1(buf);

        // Sumu počítáme z originální statistiky, nebo z této seřazené (je to stejné)
        sum += sorted_records[n].count;
    }

    sendStringUART1("---------------------\r\n");
    sprintf(buf, "TOTAL OK : %lu\r\n", sum);
    sendStringUART1(buf);
    sprintf(buf, "  ERRORS : %lu\r\n", stat.error_count);
    sendStringUART1(buf);
    sendStringUART1("---------------------\r\n");
}

void add_to_statitic (unsigned char RXnet, unsigned char RXdau)
{
	unsigned char n;
	for (n=0; n<MAX_STATISTICS_RECORDS; n++)
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

void err_to_statitic (void)
{
	stat.error_count++;
}

//------------------------------------------------------------------------------
//  Ulozeni dat ve FLASH
//------------------------------------------------------------------------------
// Vzájemné oddělení je zajištěno adresami:
// param → 0x0FE00000 (USERDATA stránka 1, velikost 2 kB)
// stat → 0x0FE00800 (USERDATA stránka 2, velikost 2 kB)
// MSC_ErasePage() maže vždy přesně jednu stránku (2 kB)
// zápis do jedné stránky tedy nikdy neovlivní druhou.
// Obě funkce jsou zcela nezávislé.
//
// Každá funkce pracuje s jinou adresou a jiným magic number:
// parameters_save/load() — adresa 0x0FE00000, magic 0x54434900
// statistic_save/load() — adresa 0x0FE00800, magic 0x53544100
//
// MSC_ErasePage() maže vždy přesně jednu 2 kB stránku,
// takže mazání statistiky na 0x0FE00800 se nijak nedotkne
// parametrů na 0x0FE00000 a naopak.
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
// Ulozeni statistiky do FLASH (USERDATA stranka 2)
// USERDATA stranka 1: 0x0FE00000 - parametry
// USERDATA stranka 2: 0x0FE00800 - statistika
// Vraci: 0=OK, 1=chyba mazani, 2=chyba zapisu
//------------------------------------------------------------------------------
#define STAT_MAGIC      0x53544100UL   // 'STA\0'
#define STAT_FLASH_ADDR ((uint32_t *)(USERDATA_BASE + 0x800))

unsigned char statistic_save(void)
{
    MSC_Status_TypeDef ret;

    uint32_t dataSize = sizeof(type_statistical);
    uint32_t bufSize  = 8 + dataSize;
    if (bufSize % 4 != 0) bufSize += 4 - (bufSize % 4);

    uint8_t buf[bufSize];
    memset(buf, 0xFF, bufSize);

    ((uint32_t *)buf)[0] = STAT_MAGIC;
    ((uint32_t *)buf)[1] = dataSize;
    memcpy(buf + 8, &stat, sizeof(type_statistical));

    MSC_Init();

    ret = MSC_ErasePage(STAT_FLASH_ADDR);
    if (ret != mscReturnOk) {
        MSC_Deinit();
        sendStringUART1("FLASH: stat erase error\r\n");
        return 1;
    }

    ret = MSC_WriteWord(STAT_FLASH_ADDR, buf, bufSize);
    if (ret != mscReturnOk) {
        MSC_Deinit();
        sendStringUART1("FLASH: stat write error\r\n");
        return 2;
    }

    MSC_Deinit();
    sendStringUART1("FLASH: statistic saved\r\n");
    return 0;
}

//------------------------------------------------------------------------------
// Nacteni statistiky z FLASH (USERDATA stranka 2)
// Vraci: 0=OK, 1=neplatna data (magic), 2=nekompatibilni velikost

unsigned char statistic_load(void)
{
    uint32_t *flash = STAT_FLASH_ADDR;

    if (flash[0] != STAT_MAGIC) {
        sendStringUART1("FLASH: no valid statistic\r\n");
        return 1;
    }

    uint32_t savedSize = flash[1];
    if (savedSize != sizeof(type_statistical)) {
        sendStringUART1("FLASH: stat size mismatch\r\n");
        return 2;
    }

    memcpy(&stat, (uint8_t *)flash + 8, sizeof(type_statistical));
    sendStringUART1("FLASH: statistic loaded\r\n");
    return 0;
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
