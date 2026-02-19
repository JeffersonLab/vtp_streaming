#ifndef VTPCONFIG_H
#define VTPCONFIG_H

#include "vtpLib.h"

/****************************************************************************
 *
 *  vtpConfig.h  -  configuration library header file for VTP board
 *
 */


#define FNLEN     250       /* length of config. file name - careful, sscanf
                               format needs to be updated when this changes */
#define STRLEN    250       /* length of str_tmp */
#define ROCLEN     80       /* length of ROC_name */

typedef struct
{
  int ssp_strigger_bit_mask[2];
  int ssp_sector_mask[2];
  int sector_mult_min[2];
  int sector_coin_width;
  int ssp_ctrigger_bit_mask;
  int delay;
  float pulser_freq;
  int prescale;
} trgbit;

typedef struct
{
  int cluster_emin;
  int cluster_emax;
  int cluster_nmin;
  int cluster_xmin;
  float pde_c[4];

  int cluster_emin_en;
  int cluster_emax_en;
  int cluster_nmin_en;
  int cluster_xmin_en;
  int pde_en;
  int hodo_l1_en;
  int hodo_l2_en;
  int hodo_l1l2_geom_en;
  int hodo_l1l2x_geom_en;
  int en;
} hps_single_trig;

typedef struct
{
  int cluster_emin;
  int cluster_emax;
  int cluster_nmin;

  int pair_dt;
  int pair_esum_min;
  int pair_esum_max;
  int pair_ediff_max;
  float pair_ed_factor;
  int pair_ed_thr;
  int pair_coplanarity_tol;

  int pair_esum_en;
  int pair_ediff_en;
  int pair_ed_en;
  int pair_coplanarity_en;
  int hodo_l1_en;
  int hodo_l2_en;
  int hodo_l1l2_geom_en;
  int hodo_l1l2x_geom_en;
  int en;
} hps_pair_trig;

typedef struct
{
  int cluster_emin;
  int cluster_emax;
  int cluster_nmin;
  int mult_dt;
  int mult_top_min;
  int mult_bot_min;
  int mult_tot_min;
  int en;
} hps_mult_trig;

typedef struct
{
  int mask_en;
  int nstreams;
  int connect;
  unsigned char ipaddr[4];
  unsigned char subnet[4];
  unsigned char gateway[4];
  unsigned char mac[6];
  unsigned char destip[4];
  unsigned short destipport;
  unsigned short localport;
} streaming_eb_cfg;

typedef struct
{
  unsigned int connect;
  unsigned int ipaddr;
  unsigned int subnet;
  unsigned int gateway;
  unsigned char mac[6];
} roc_tcp_cfg;


/** VTP configuration parameters **/
typedef struct {
  char fw_filename_v7[FNLEN];
  char fw_filename_z7[FNLEN];

  int fw_rev[2];
  int fw_type[2];
  int refclk;

  int window_width;
  int window_offset;

  int payload_en;
  int fiber_en;

  struct
  {
    int roc_id;
    unsigned int destip;
    unsigned int destipport;
    unsigned int nwords;
    unsigned int ebdata[20];
    roc_tcp_cfg eb;
  } vtp_roc;


  struct
  {
    int roc_id;
    int nframe_buf;
    int frame_len;
    streaming_eb_cfg eb[2];
  } fadc_streaming;

  struct
  {
    unsigned int fadcsum_ch_en[16];
    struct
    {
      int hit_emin;
      int hit_dt;
      int dalitz_min;
      int dalitz_max;
      int cosmic_emin;
      int cosmic_multmax;
      int cosmic_hitwidth;
      int cosmic_evaldelay;
    } inner;
    struct
    {
      int hit_emin;
      int hit_dt;
      int dalitz_min;
      int dalitz_max;
      int cosmic_emin;
      int cosmic_multmax;
      int cosmic_hitwidth;
      int cosmic_evaldelay;
    } outer;
  } ec;

  struct
  {
    unsigned int fadcsum_ch_en[16];
    int cosmic_emin;
    int cosmic_multmax;
    int cosmic_hitwidth;
    int cosmic_evaldelay;
    int cosmic_pixelen;
  } pc;

  struct
  {
    int threshold[3];
    int nframes;
    int dipfactor;
    int dalitz_min;
    int dalitz_max;
    int nstrip_min;
    int nstrip_max;
    int pcu_threshold[3];
    int cosmic_emin;
    int cosmic_multmax;
    int cosmic_hitwidth;
    int cosmic_evaldelay;
    int cosmic_pixelen;
  } pcs;

  struct
  {
    int threshold[3];
    int nframes;
    int ctof_threshold[3];
    int ctof_nframes;
  } htcc;

  struct
  {
    int threshold[3];
    int nframes;
  } ftof;

  struct
  {
    int threshold[3];
    int nframes;
  } cnd;

  struct
  {
    unsigned int fadcsum_ch_en[16];
    int threshold[3];
    int nframes;
    int dipfactor;
    int dalitz_min;
    int dalitz_max;
    int nstrip_min;
    int nstrip_max;
    struct
    {
      int cosmic_emin;
      int cosmic_multmax;
      int cosmic_hitwidth;
      int cosmic_evaldelay;
    } inner;
    struct
    {
      int cosmic_emin;
      int cosmic_multmax;
      int cosmic_hitwidth;
      int cosmic_evaldelay;
    } outer;
  } ecs;

  struct
  {
    int trig_latency;
    int trig_width;
    trgbit trgbits[32];
  } gt;

  struct
  {
    int dcsegfind_threshold[2];
    char roadid[9];
  } dc;

  struct
  {
    int hit_dt;
    int cluster_emin;
  } hcal;

  struct
  {
    unsigned int fadcsum_ch_en[16];
    int seed_emin;
    int seed_dt;
    int hodo_dt;
    int deadtime;
    int deadtime_emin;
  } ftcal;

  struct
  {
    int hit_emin;
  } fthodo;

  struct
  {
    struct
    {
      int top_nbottom;
      int hit_dt;
      int seed_thr;
    } cluster;

    struct
    {
      int hit_dt;
      int fadchit_thr;
      int hodo_thr;
    } hodoscope;

    struct
    {
      int hodoscope_top_en;
      int hodoscope_bot_en;

      int cosmic_dt;
      int cosmic_top_en;
      int cosmic_bot_en;

      float pulser_freq;
      int pulser_en;
    } calib;

    hps_single_trig single_trig[4];

    hps_pair_trig pair_trig[4];

    hps_mult_trig mult_trig[2];

    struct
    {
      int cluster_emin;
      int cluster_emax;
      int cluster_nmin;
      int prescale_xmin[7];
      int prescale_xmax[7];
      int prescale[7];
      int en;
    } fee_trig;

    struct
    {
      int latency;
      int prescale[32];
    } trig;
  } hps;

  struct
  {
    int enable_scaler_readout;
    int vetroc_width;
    int fadc_threshold[5];
    int eplane_mult_min[5];
    int eplane_mask[5];
    int fadc_mask[5];

    struct
    {
      int latency;
      int width;
      int prescale[32];
	  int delay[32];
    } trig;

  } compton;

} VTP_CONF;

/* functions */
void vtpSetExpid(char *string);
void vtpInitGlobals();
int vtpReadConfigFile(char *filename);
int vtpDownloadAll();
int vtpUploadAll(char *string, int length);
int vtpConfig(char *fname);
void vtpMon();

#endif
