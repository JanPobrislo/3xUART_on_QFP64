#ifndef PARAMETERS_H
#define PARAMETERS_H

#define PROGRAM_VERSION "0.1"

#define MAX_NETS      15
#define MAX_ROUTES    20

typedef struct {
	unsigned char net;
	unsigned char path;
	unsigned char dau;
	unsigned char follow;
	unsigned char error;
	unsigned char revers;
} tci_routes;

typedef struct {
	unsigned char primary_net;
	unsigned char next_time;
	unsigned char next_rpt;
	unsigned char error_rpt;
	unsigned char pretime;
	unsigned char deadtime;
	unsigned char sys_tok;
	unsigned char out_enabled;
	unsigned char netdau[MAX_NETS];
	tci_routes    route[MAX_ROUTES];
} tci_parameters;

extern tci_parameters param;

typedef struct {
	unsigned char follow;	// Adresat (DAU) v prime ceste
	unsigned char error;	// Adresat (DAU) v chybove ceste
	unsigned char revers;	// Adresat (DAU) v reverzni ceste
} routes_for_tx;

extern routes_for_tx tx_route;  // definuje routu pro vysilani

extern unsigned long uptime;    // cas od resetu v sec

#define TIME_TO_SET_RTC 7200    // po kolika sec nastavi RTC z GPS
#define TIME_TO_SAVE_STATISTIC 86400  // po kolika sec zapise statistiku do flash (24hod)

//------------------------------------------------------------------------------
// Docasne globalni parametry - Prepinace pro zobrazovani na consoli COM-B UART1
//------------------------------------------------------------------------------
extern unsigned char show_timetick;
extern unsigned char show_inputs;
extern unsigned char show_gps_nmea;
extern unsigned char show_rxtx_details;

unsigned char parameters_load(void);
unsigned char parameters_save(void);

void parameters_clear_netdau(void);
void parameters_clear_routes(void);
void parameters_init(void);
void parameters_show(void);
unsigned char make_tx_route(unsigned char net, unsigned char path, unsigned char dau);

void init_statistic(void);
void clear_statistic(void);
void show_statistic(void);
void add_to_statitic(unsigned char RXnet, unsigned char RXdau);
void err_to_statitic (void);
unsigned char statistic_save(void);
unsigned char statistic_load(void);

#endif
