
/* vtpConfig.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "vtpConfig.h"
#include "vtpLib.h"
//#include "xxxConfig.h"

//#undef DEBUG
#define DEBUG

#define ADD_TO_STRING				\
  len1 = strlen(str);				\
  len2 = strlen(sss);				\
  if((len1+len2) < length) strcat(str,sss);	\
  else return(len1)

#define CLOSE_STRING				\
  len1 = strlen(str);				\
  return(len1)

static int active;

static VTP_CONF vtpConf;

char *getenv();

#define SCAN_MSK						\
  args = sscanf (str_tmp, "%*s %d %d %d %d %d %d %d %d   \
                                     %d %d %d %d %d %d %d %d",	\
		 &msk[ 0], &msk[ 1], &msk[ 2], &msk[ 3],	\
		 &msk[ 4], &msk[ 5], &msk[ 6], &msk[ 7],	\
		 &msk[ 8], &msk[ 9], &msk[10], &msk[11],	\
		 &msk[12], &msk[13], &msk[14], &msk[15])

#define SCAN_FMSK						\
  args = sscanf (str_tmp, "%*s %f %f %f %f %f %f %f %f   \
                                     %f %f %f %f %f %f %f %f",	\
		 &fmsk[ 0], &fmsk[ 1], &fmsk[ 2], &fmsk[ 3],	\
		 &fmsk[ 4], &fmsk[ 5], &fmsk[ 6], &fmsk[ 7],	\
		 &fmsk[ 8], &fmsk[ 9], &fmsk[10], &fmsk[11],	\
		 &fmsk[12], &fmsk[13], &fmsk[14], &fmsk[15])

#define GET_READ_MSK							\
  SCAN_MSK;								\
  ui1 = 0;								\
  for(jj=0; jj<16; jj++)						\
    {									\
      if((msk[jj] < 0) || (msk[jj] > 1))				\
	{								\
	  printf("\nReadConfigFile: Wrong mask bit value, %d\n\n",msk[jj]); return(-6); \
	}								\
      if(strcmp(keyword,"FADC250_ADC_MASK") == 0) msk[jj] = ~(msk[jj])&0x1; \
      ui1 |= (msk[jj]<<jj);						\
    }

#define SCAN_MSK4						\
  args = sscanf (str_tmp, "%*s %d %d %d %d",			\
		 &msk[ 0], &msk[ 1], &msk[ 2], &msk[ 3])

#define GET_READ_MSK4							\
  SCAN_MSK4;								\
  ui1 = 0;								\
  for(jj=0; jj<4; jj++)							\
    {									\
      if((msk[jj] < 0) || (msk[jj] > 1))				\
	{								\
	  printf("\nReadConfigFile: Wrong mask bit value, %d\n\n",msk[jj]); return(-6); \
	}								\
      ui1 |= (msk[jj]<<jj);						\
    }

#define SCAN_MSK4 \
  args = sscanf (str_tmp, "%*s %d %d %d %d", \
           &msk[ 0], &msk[ 1], &msk[ 2], &msk[ 3])

#define GET_READ_MSK4 \
  SCAN_MSK4; \
  ui1 = 0; \
  for(jj=0; jj<4; jj++) \
  { \
    if((msk[jj] < 0) || (msk[jj] > 1)) \
    { \
      printf("\nReadConfigFile: Wrong mask bit value, %d\n\n",msk[jj]); return(-6); \
    } \
    ui1 |= (msk[jj]<<jj); \
  }

#define SCAN_MSK8 \
  args = sscanf (str_tmp, "%*s %d %d %d %d %d %d %d %d", \
           &msk[ 0], &msk[ 1], &msk[ 2], &msk[ 3], \
           &msk[ 4], &msk[ 5], &msk[ 6], &msk[ 7])

#define GET_READ_MSK8 \
  SCAN_MSK8; \
  ui1 = 0; \
  for(jj=0; jj<8; jj++) \
  { \
    if((msk[jj] < 0) || (msk[jj] > 1)) \
    { \
      printf("\nReadConfigFile: Wrong mask bit value, %d\n\n",msk[jj]); return(-6); \
    } \
    ui1 |= (msk[jj]<<jj); \
  }

static char *expid = NULL;

/* Routine prototype */
int vtpUploadAllPrint();


void
vtpSetExpid(char *string)
{
  expid = strdup(string);
}

int
vtpConfig(char *fname)
{
  int res;

  /* Do this call outside of this routine for testing - DJA*/
  //vtpInitGlobals();

  /* reading and parsing config file */
  if( (res = vtpReadConfigFile(fname)) < 0 )
    {
      printf("%s: WARNING: vtpReadConfigFile() returned %d\n\t\tUsing defaults.",
	     __func__, res);
    }

  /* download to all boards */
  vtpDownloadAll();

  return(0);
}

void
vtpInitGlobals()
{
  int i;

  printf("vtpInitGlobals reached\n");

  memset(vtpConf.fw_filename_v7, 0, sizeof(vtpConf.fw_filename_v7));
  memset(vtpConf.fw_filename_z7, 0, sizeof(vtpConf.fw_filename_z7));

  /* V7 and Z7 firmware */
  for(i=0;i<2;i++) {
    vtpConf.fw_rev[i] = -1;
    vtpConf.fw_type[i] = -1;
  }

  vtpConf.window_width = 0;
  vtpConf.window_offset = 0;

  vtpConf.refclk = 250;

  vtpConf.payload_en = 0;
  vtpConf.fiber_en = 0;

  // HTCC configuration
  vtpConf.htcc.threshold[0] = 0;
  vtpConf.htcc.threshold[1] = 0;
  vtpConf.htcc.threshold[2] = 0;
  vtpConf.htcc.nframes = 0;

  vtpConf.htcc.ctof_threshold[0] = 0;
  vtpConf.htcc.ctof_threshold[1] = 0;
  vtpConf.htcc.ctof_threshold[2] = 0;
  vtpConf.htcc.ctof_nframes = 0;

  // FTOF configuration
  vtpConf.ftof.threshold[0] = 0;
  vtpConf.ftof.threshold[1] = 0;
  vtpConf.ftof.threshold[2] = 0;
  vtpConf.ftof.nframes = 0;

  // CND configuration
  vtpConf.cnd.threshold[0] = 0;
  vtpConf.cnd.threshold[1] = 0;
  vtpConf.cnd.threshold[2] = 0;
  vtpConf.cnd.nframes = 0;

  // EC configuration
  for(i=0; i<16; i++)
    vtpConf.ec.fadcsum_ch_en[i] = 0;

  vtpConf.ec.inner.hit_emin = 0;
  vtpConf.ec.inner.hit_dt = 0;
  vtpConf.ec.inner.dalitz_min = 0;
  vtpConf.ec.inner.dalitz_max = 0;
  vtpConf.ec.inner.cosmic_emin = 0;
  vtpConf.ec.inner.cosmic_multmax = 0;
  vtpConf.ec.inner.cosmic_hitwidth = 0;
  vtpConf.ec.inner.cosmic_evaldelay = 0;

  vtpConf.ec.outer.hit_emin = 0;
  vtpConf.ec.outer.hit_dt = 0;
  vtpConf.ec.outer.dalitz_min = 0;
  vtpConf.ec.outer.dalitz_max = 0;
  vtpConf.ec.outer.cosmic_emin = 0;
  vtpConf.ec.outer.cosmic_multmax = 0;
  vtpConf.ec.outer.cosmic_hitwidth = 0;
  vtpConf.ec.outer.cosmic_evaldelay = 0;


  // PC configuration
  for(i=0; i<16; i++)
    vtpConf.pc.fadcsum_ch_en[i] = 0;

  vtpConf.pc.cosmic_emin = 0;
  vtpConf.pc.cosmic_multmax = 0;
  vtpConf.pc.cosmic_hitwidth = 0;
  vtpConf.pc.cosmic_evaldelay = 0;
  vtpConf.pc.cosmic_pixelen = 0;


  // PCS configuration
  vtpConf.pcs.threshold[0] = 0;
  vtpConf.pcs.threshold[1] = 0;
  vtpConf.pcs.threshold[2] = 0;
  vtpConf.pcs.nframes = 0;
  vtpConf.pcs.dipfactor = 0;
  vtpConf.pcs.dalitz_min = 0;
  vtpConf.pcs.dalitz_max = 0;
  vtpConf.pcs.nstrip_min = 0;
  vtpConf.pcs.nstrip_max = 0;
  vtpConf.pcs.pcu_threshold[0] = 0;
  vtpConf.pcs.pcu_threshold[1] = 0;
  vtpConf.pcs.pcu_threshold[2] = 0;
  vtpConf.pcs.cosmic_emin = 0;
  vtpConf.pcs.cosmic_multmax = 0;
  vtpConf.pcs.cosmic_hitwidth = 0;
  vtpConf.pcs.cosmic_evaldelay = 0;
  vtpConf.pcs.cosmic_pixelen = 0;

  // ECS configuration
  for(i=0; i<16; i++)
    vtpConf.ecs.fadcsum_ch_en[i] = 0;

  vtpConf.ecs.inner.cosmic_emin = 0;
  vtpConf.ecs.inner.cosmic_multmax = 0;
  vtpConf.ecs.inner.cosmic_hitwidth = 0;
  vtpConf.ecs.inner.cosmic_evaldelay = 0;

  vtpConf.ecs.outer.cosmic_emin = 0;
  vtpConf.ecs.outer.cosmic_multmax = 0;
  vtpConf.ecs.outer.cosmic_hitwidth = 0;
  vtpConf.ecs.outer.cosmic_evaldelay = 0;

  vtpConf.ecs.threshold[0] = 0;
  vtpConf.ecs.threshold[1] = 0;
  vtpConf.ecs.threshold[2] = 0;
  vtpConf.ecs.nframes = 0;
  vtpConf.ecs.dipfactor = 0;
  vtpConf.ecs.dalitz_min = 0;
  vtpConf.ecs.dalitz_max = 0;
  vtpConf.ecs.nstrip_min = 0;
  vtpConf.ecs.nstrip_max = 0;


  // GT configuration
  vtpConf.gt.trig_latency = 1000;
  vtpConf.gt.trig_width = 100;

  for(i=0; i<32; i++)
    {
      vtpConf.gt.trgbits[i].ssp_strigger_bit_mask[0] = 0;
      vtpConf.gt.trgbits[i].ssp_strigger_bit_mask[1] = 0;
      vtpConf.gt.trgbits[i].ssp_sector_mask[0] = 0;
      vtpConf.gt.trgbits[i].ssp_sector_mask[1] = 0;
      vtpConf.gt.trgbits[i].sector_mult_min[0] = 0;
      vtpConf.gt.trgbits[i].sector_mult_min[1] = 0;
      vtpConf.gt.trgbits[i].sector_coin_width = 0;
      vtpConf.gt.trgbits[i].ssp_ctrigger_bit_mask = 0;
      vtpConf.gt.trgbits[i].pulser_freq = 0;
      vtpConf.gt.trgbits[i].delay = 0;
      vtpConf.gt.trgbits[i].prescale = 1;
    }

  // DC configuration
  vtpConf.dc.dcsegfind_threshold[0] = 0;
  vtpConf.dc.dcsegfind_threshold[1] = 0;
  memset(vtpConf.dc.roadid, 0, sizeof(vtpConf.dc.roadid));

  // HCAL configuration
  vtpConf.hcal.hit_dt = 0;
  vtpConf.hcal.cluster_emin = 0;

  // FTCAL configuration
  for(i=0; i<16; i++)
    vtpConf.ftcal.fadcsum_ch_en[i] = 0;

  vtpConf.ftcal.seed_emin = 0;
  vtpConf.ftcal.seed_dt = 0;
  vtpConf.ftcal.deadtime = 0;
  vtpConf.ftcal.deadtime_emin = 0x3FFF;

  // HPS configuration
  vtpConf.hps.cluster.top_nbottom = 0;
  vtpConf.hps.cluster.hit_dt = 0;
  vtpConf.hps.cluster.seed_thr = 0;

  vtpConf.hps.hodoscope.hit_dt = 0;
  vtpConf.hps.hodoscope.fadchit_thr = 0;
  vtpConf.hps.hodoscope.hodo_thr = 0;

  vtpConf.hps.calib.hodoscope_top_en = 0;
  vtpConf.hps.calib.hodoscope_bot_en = 0;
  vtpConf.hps.calib.cosmic_dt = 0;
  vtpConf.hps.calib.cosmic_top_en = 0;
  vtpConf.hps.calib.cosmic_bot_en = 0;
  vtpConf.hps.calib.pulser_freq = 0;
  vtpConf.hps.calib.pulser_en = 0;

  for(i=0;i<4;i++)
    {
      vtpConf.hps.single_trig[i].cluster_emin = 1;
      vtpConf.hps.single_trig[i].cluster_emax = 8191;
      vtpConf.hps.single_trig[i].cluster_nmin = 1;
      vtpConf.hps.single_trig[i].cluster_xmin = -31;
      vtpConf.hps.single_trig[i].pde_c[0] = 0;
      vtpConf.hps.single_trig[i].pde_c[1] = 0;
      vtpConf.hps.single_trig[i].pde_c[2] = 0;
      vtpConf.hps.single_trig[i].pde_c[3] = 0;

      vtpConf.hps.single_trig[i].cluster_emin_en = 0;
      vtpConf.hps.single_trig[i].cluster_emax_en = 0;
      vtpConf.hps.single_trig[i].cluster_nmin_en = 0;
      vtpConf.hps.single_trig[i].cluster_xmin_en = 0;
      vtpConf.hps.single_trig[i].pde_en = 0;
      vtpConf.hps.single_trig[i].hodo_l1_en = 0;
      vtpConf.hps.single_trig[i].hodo_l2_en = 0;
      vtpConf.hps.single_trig[i].hodo_l1l2_geom_en = 0;
      vtpConf.hps.single_trig[i].hodo_l1l2x_geom_en = 0;
      vtpConf.hps.single_trig[i].en = 0;
    }

  for(i=0;i<4;i++)
    {
      vtpConf.hps.pair_trig[i].cluster_emin = 1;
      vtpConf.hps.pair_trig[i].cluster_emax = 8191;
      vtpConf.hps.pair_trig[i].cluster_nmin = 1;

      vtpConf.hps.pair_trig[i].pair_dt = 0;
      vtpConf.hps.pair_trig[i].pair_esum_min = 0;
      vtpConf.hps.pair_trig[i].pair_esum_max = 16383;
      vtpConf.hps.pair_trig[i].pair_ediff_max = 8191;
      vtpConf.hps.pair_trig[i].pair_ed_factor = 0.0;
      vtpConf.hps.pair_trig[i].pair_ed_thr = 0;
      vtpConf.hps.pair_trig[i].pair_coplanarity_tol = 0;

      vtpConf.hps.pair_trig[i].pair_esum_en = 0;
      vtpConf.hps.pair_trig[i].pair_ediff_en = 0;
      vtpConf.hps.pair_trig[i].pair_ed_en = 0;
      vtpConf.hps.pair_trig[i].pair_coplanarity_en = 0;
      vtpConf.hps.pair_trig[i].hodo_l1_en = 0;
      vtpConf.hps.pair_trig[i].hodo_l2_en = 0;
      vtpConf.hps.pair_trig[i].hodo_l1l2_geom_en = 0;
      vtpConf.hps.pair_trig[i].hodo_l1l2x_geom_en = 0;
      vtpConf.hps.pair_trig[i].en = 0;
    }

  for(i=0;i<4;i++)
    {
      vtpConf.hps.mult_trig[i].cluster_emin = 0;
      vtpConf.hps.mult_trig[i].cluster_emax = 0;
      vtpConf.hps.mult_trig[i].cluster_nmin = 0;
      vtpConf.hps.mult_trig[i].mult_dt = 0;
      vtpConf.hps.mult_trig[i].mult_top_min = 0;
      vtpConf.hps.mult_trig[i].mult_bot_min = 0;
      vtpConf.hps.mult_trig[i].mult_tot_min = 0;
      vtpConf.hps.mult_trig[i].en = 0;
    }

  vtpConf.hps.fee_trig.cluster_emin = 1;
  vtpConf.hps.fee_trig.cluster_emax = 8191;
  vtpConf.hps.fee_trig.cluster_nmin = 1;
  vtpConf.hps.fee_trig.en = 0;
  for(i=0;i<7;i++)
    {
      vtpConf.hps.fee_trig.prescale_xmin[i] = -31;
      vtpConf.hps.fee_trig.prescale_xmax[i] = -31;
      vtpConf.hps.fee_trig.prescale[i] = 0;
    }

  vtpConf.hps.trig.latency = 0;
  for(i=0;i<32;i++)
    vtpConf.hps.trig.prescale[i] = 1;

  // FADC Streaming configuration
  vtpConf.fadc_streaming.roc_id = 0;
  vtpConf.fadc_streaming.nframe_buf = 1000;
  vtpConf.fadc_streaming.frame_len = 65536;
  vtpConf.fadc_streaming.eb[0].mask_en = 0;
  vtpConf.fadc_streaming.eb[0].nstreams = 0;
  vtpConf.fadc_streaming.eb[0].connect = 0;
  vtpConf.fadc_streaming.eb[0].ipaddr[0] = 0;
  vtpConf.fadc_streaming.eb[0].ipaddr[1] = 0;
  vtpConf.fadc_streaming.eb[0].ipaddr[2] = 0;
  vtpConf.fadc_streaming.eb[0].ipaddr[3] = 0;
  vtpConf.fadc_streaming.eb[0].subnet[0] = 0;
  vtpConf.fadc_streaming.eb[0].subnet[1] = 0;
  vtpConf.fadc_streaming.eb[0].subnet[2] = 0;
  vtpConf.fadc_streaming.eb[0].subnet[3] = 0;
  vtpConf.fadc_streaming.eb[0].gateway[0] = 0;
  vtpConf.fadc_streaming.eb[0].gateway[1] = 0;
  vtpConf.fadc_streaming.eb[0].gateway[2] = 0;
  vtpConf.fadc_streaming.eb[0].gateway[3] = 0;
  vtpConf.fadc_streaming.eb[0].mac[0] = 0;
  vtpConf.fadc_streaming.eb[0].mac[1] = 0;
  vtpConf.fadc_streaming.eb[0].mac[2] = 0;
  vtpConf.fadc_streaming.eb[0].mac[3] = 0;
  vtpConf.fadc_streaming.eb[0].mac[4] = 0;
  vtpConf.fadc_streaming.eb[0].mac[5] = 0;
  vtpConf.fadc_streaming.eb[0].destip[0] = 0;
  vtpConf.fadc_streaming.eb[0].destip[1] = 0;
  vtpConf.fadc_streaming.eb[0].destip[2] = 0;
  vtpConf.fadc_streaming.eb[0].destip[3] = 0;
  vtpConf.fadc_streaming.eb[0].destipport = 0;
  vtpConf.fadc_streaming.eb[1].mask_en = 0;
  vtpConf.fadc_streaming.eb[1].nstreams = 0;
  vtpConf.fadc_streaming.eb[1].connect = 0;
  vtpConf.fadc_streaming.eb[1].ipaddr[0] = 0;
  vtpConf.fadc_streaming.eb[1].ipaddr[1] = 0;
  vtpConf.fadc_streaming.eb[1].ipaddr[2] = 0;
  vtpConf.fadc_streaming.eb[1].ipaddr[3] = 0;
  vtpConf.fadc_streaming.eb[1].subnet[0] = 0;
  vtpConf.fadc_streaming.eb[1].subnet[1] = 0;
  vtpConf.fadc_streaming.eb[1].subnet[2] = 0;
  vtpConf.fadc_streaming.eb[1].subnet[3] = 0;
  vtpConf.fadc_streaming.eb[1].gateway[0] = 0;
  vtpConf.fadc_streaming.eb[1].gateway[1] = 0;
  vtpConf.fadc_streaming.eb[1].gateway[2] = 0;
  vtpConf.fadc_streaming.eb[1].gateway[3] = 0;
  vtpConf.fadc_streaming.eb[1].mac[0] = 0;
  vtpConf.fadc_streaming.eb[1].mac[1] = 0;
  vtpConf.fadc_streaming.eb[1].mac[2] = 0;
  vtpConf.fadc_streaming.eb[1].mac[3] = 0;
  vtpConf.fadc_streaming.eb[1].mac[4] = 0;
  vtpConf.fadc_streaming.eb[1].mac[5] = 0;
  vtpConf.fadc_streaming.eb[1].destip[0] = 0;
  vtpConf.fadc_streaming.eb[1].destip[1] = 0;
  vtpConf.fadc_streaming.eb[1].destip[2] = 0;
  vtpConf.fadc_streaming.eb[1].destip[3] = 0;
  vtpConf.fadc_streaming.eb[1].destipport = 0;

  // VTP ROC Configuration
  vtpConf.vtp_roc.roc_id = 0;
  vtpConf.vtp_roc.destip = 0;
  vtpConf.vtp_roc.destipport = 0;
  vtpConf.vtp_roc.nwords = 0;
  for(i=0;i<20;i++)
    vtpConf.vtp_roc.ebdata[i] = 0;
  vtpConf.vtp_roc.eb.connect = 0;
  vtpConf.vtp_roc.eb.ipaddr = 0;
  vtpConf.vtp_roc.eb.subnet = 0;
  vtpConf.vtp_roc.eb.gateway = 0;
  vtpConf.vtp_roc.eb.mac[0] = 0;
  vtpConf.vtp_roc.eb.mac[1] = 0;
  vtpConf.vtp_roc.eb.mac[2] = 0;
  vtpConf.vtp_roc.eb.mac[3] = 0;
  vtpConf.vtp_roc.eb.mac[4] = 0;
  vtpConf.vtp_roc.eb.mac[5] = 0;


  // Compton Configuration
  vtpConf.compton.trig.latency = 1000;
  vtpConf.compton.trig.width = 100;
  vtpConf.compton.vetroc_width = 2;
  vtpConf.compton.enable_scaler_readout = 0;
  for(i=0; i<5; i++)
  {
    vtpConf.compton.fadc_threshold[i] = 0;
    vtpConf.compton.fadc_mask[i] = 0;
    vtpConf.compton.eplane_mult_min[i] = 0;
    vtpConf.compton.eplane_mask[i] = 0;
  }
  for(i=0;i<32;i++)
  {
    vtpConf.compton.trig.prescale[i] = 0;
    vtpConf.compton.trig.delay[i] = 0;
  }
}


/* reading and parsing config file */
int
vtpReadConfigFile(char *filename_in)
{
  FILE   *fd;
  char   filename[FNLEN];
  char   fname[FNLEN] = { "" };  /* config file name */
  int    jj, ch;
  char   str_tmp[STRLEN], keyword[ROCLEN];
  char   host[ROCLEN], ROC_name[ROCLEN];
  int    argc,args,argi[11],msk[16],trg_bit, streaming_eb=0;
  float  argf[4];
  unsigned int  ui1;
  char *envDir;
  int do_parsing;

  gethostname(host,ROCLEN);  /* obtain our hostname - and drop any domain extension */
  for(jj=0; jj<strlen(host); jj++)
    {
      if(host[jj] == '.')
	{
	  host[jj] = '\0';
	  break;
	}
    }

#ifdef VTP_CONFIG_GET_ENV
  printf("%s: INFO: checking for config path environment variable: %s\n",
	 __func__,VTP_CONFIG_GET_ENV);
  envDir = getenv(VTP_CONFIG_GET_ENV);
  if(envDir == NULL)
    {
      strcpy((char *)str_tmp,"./");
      envDir = (char *)str_tmp;
      printf("%s: INFO: %s not found. Using %s\n",
	     __func__,VTP_CONFIG_GET_ENV,envDir);
    }
  else
    {
      printf("%s: VTP Config Environment Variable:\n"
	     " %s = %s\n",
	     __func__,
	     VTP_CONFIG_GET_ENV, envDir);
    }
#else
  strcpy((char *)str_tmp,"./");
  envDir = (char *)str_tmp;
#endif


  /* printf("\n%s: using config file >%s<\n\n", */
  /* 	 __func__, filename_in); */

  if(expid==NULL)
    {
      expid = getenv("EXPID");
      printf("%s: INFO: using EXPID=>%s< from environment\n",__func__,expid);
    }
  else
    {
      printf("%s: INFO: using EXPID=>%s< from CODA\n",__func__,expid);
    }

  strcpy(filename,filename_in); /* copy filename from parameter list to local string */
  do_parsing = 1;


  while(do_parsing)
    {
      if(strlen(filename)!=0) /* filename specified */
	{
	  if ( filename[0]=='/' || (filename[0]=='.' && filename[1]=='/') )
	    {
	      sprintf(fname, "%s", filename);
	    }
	  else
	    {
	      sprintf(fname, "%s/vtp/%s", envDir, filename);
	    }

	  if((fd=fopen(fname,"r")) == NULL)
	    {
	      printf("\nReadConfigFile: Can't open config file >%s<\n",fname);
	      return(-1);
	    }
	}
      else if(do_parsing<2) /* filename does not specified */
	{
	  sprintf(fname, "%s/vtp/%s.cnf", envDir, host);
	  printf("%s: Trying %s\n",
		 __func__,fname);
	  if((fd=fopen(fname,"r")) == NULL)
	    {
	      sprintf(fname, "%s/vtp/%s.cnf",  envDir, expid);
	      printf("%s: Trying %s\n",
		     __func__,fname);
	      if((fd=fopen(fname,"r")) == NULL)
		{
		  printf("%s: Can't find config file\n",__func__);
		  return(-2);
		}
	    }
	}
      else
	{
	  printf("\nReadConfigFile: ERROR: since do_parsing=%d (>1), filename must be specified\n",do_parsing);
	  return(-1);
	}

      printf("\nReadConfigFile: Using configuration file >%s<\n",fname);

      /* Parsing of config file */
      active = 0; /* by default disable crate */
      do_parsing = 0; /* will parse only one file specified above, unless it changed during parsing */
      while ((ch = getc(fd)) != EOF)
	{
	  memset(argf, 0, sizeof(argf));
	  memset(argi, 0, sizeof(argi));
	  if ( ch == '#' || ch == ' ' || ch == '\t' )
	    {
	      while ( getc(fd)!='\n' /*&& getc(fd)!=!= EOF*/ ) {} /*ERROR !!!*/
	    }
	  else if( ch == '\n' ) {}
	  else
	    {
	      ungetc(ch,fd);
	      fgets(str_tmp, STRLEN, fd);
	      sscanf (str_tmp, "%s %s", keyword, ROC_name);
#ifdef DEBUG
	      printf("\nfgets returns %s so keyword=%s\n\n",str_tmp,keyword);
#endif
	      /* Start parsing real config inputs */
	      if(strcmp(keyword,"VTP_CRATE") == 0)
		{
		  if(strcmp(ROC_name,host) == 0)
		    {
		      printf("\nReadConfigFile: crate = %s  host = %s - activated\n",ROC_name,host);
		      active = 1;
		    }
		  else if(strcmp(ROC_name,"all") == 0)
		    {
		      printf("\nReadConfigFile: crate = %s  host = %s - activated\n",ROC_name,host);
		      active = 1;
		    }
		  else
		    {
		      printf("\nReadConfigFile: crate = %s  host = %s - deactivated\n",ROC_name,host);
		      active = 0;
		    }
		  continue;
		}

	      /* If the ROC_name does not match the hostname or the string "all" then do not parse
                 any more of the file. Just read through to the end of the file */
	      if(!active)
		continue;

	      else if(!strcmp(keyword,"VTP_W_WIDTH"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.window_width = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_W_OFFSET"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.window_offset = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_FIRMWARE_V7"))
		{
		  sscanf(str_tmp, "%*s %250s", vtpConf.fw_filename_v7);
		  printf("VTP_FIRMWARE = %s\n", vtpConf.fw_filename_v7);
		}
	      else if(!strcmp(keyword,"VTP_FIRMWARE_Z7"))
		{
		  sscanf(str_tmp, "%*s %250s", vtpConf.fw_filename_z7);
		  printf("VTP_FIRMWARE = %s\n", vtpConf.fw_filename_z7);
		}
	      else if(!strcmp(keyword,"VTP_REFCLK"))
		{
		  sscanf(str_tmp, "%*s %d", &vtpConf.refclk);
		}
	      else if(!strcmp(keyword,"VTP_PAYLOAD_EN"))
		{
		  GET_READ_MSK;
		  vtpConf.payload_en = ui1;
		}
	      else if(!strcmp(keyword,"VTP_FIBER_EN"))
		{
		  GET_READ_MSK4;
		  vtpConf.fiber_en = ui1;
		  printf("vtpConf.fiber_en = 0x%08X\n", vtpConf.fiber_en);
		}
	      else if(!strcmp(keyword,"VTP_EC_FADCSUM_CH"))
		{
		  sscanf (str_tmp, "%*s 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X",
			  &vtpConf.ec.fadcsum_ch_en[0],  &vtpConf.ec.fadcsum_ch_en[1],
			  &vtpConf.ec.fadcsum_ch_en[2],  &vtpConf.ec.fadcsum_ch_en[3],
			  &vtpConf.ec.fadcsum_ch_en[4],  &vtpConf.ec.fadcsum_ch_en[5],
			  &vtpConf.ec.fadcsum_ch_en[6],  &vtpConf.ec.fadcsum_ch_en[7],
			  &vtpConf.ec.fadcsum_ch_en[8],  &vtpConf.ec.fadcsum_ch_en[9],
			  &vtpConf.ec.fadcsum_ch_en[10], &vtpConf.ec.fadcsum_ch_en[11],
			  &vtpConf.ec.fadcsum_ch_en[12], &vtpConf.ec.fadcsum_ch_en[13],
			  &vtpConf.ec.fadcsum_ch_en[14], &vtpConf.ec.fadcsum_ch_en[15]
			  );
		}
	      else if(!strcmp(keyword,"VTP_EC_INNER_HIT_EMIN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ec.inner.hit_emin = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_EC_INNER_HIT_DT"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ec.inner.hit_dt = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_EC_INNER_HIT_DALITZ"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  vtpConf.ec.inner.dalitz_min = argi[0]*8;
		  vtpConf.ec.inner.dalitz_max = argi[1]*8;
		}
	      else if(!strcmp(keyword,"VTP_EC_INNER_COSMIC_EMIN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ec.inner.cosmic_emin = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_EC_INNER_COSMIC_MULTMAX"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ec.inner.cosmic_multmax = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_EC_INNER_COSMIC_HITWIDTH"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ec.inner.cosmic_hitwidth = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_EC_INNER_COSMIC_EVALDELAY"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ec.inner.cosmic_evaldelay = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_EC_OUTER_HIT_EMIN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ec.outer.hit_emin = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_EC_OUTER_HIT_DT"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ec.outer.hit_dt = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_EC_OUTER_HIT_DALITZ"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  vtpConf.ec.outer.dalitz_min = argi[0]*8;
		  vtpConf.ec.outer.dalitz_max = argi[1]*8;
		}
	      else if(!strcmp(keyword,"VTP_EC_OUTER_COSMIC_EMIN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ec.outer.cosmic_emin = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_EC_OUTER_COSMIC_MULTMAX"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ec.outer.cosmic_multmax = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_EC_OUTER_COSMIC_HITWIDTH"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ec.outer.cosmic_hitwidth = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_EC_OUTER_COSMIC_EVALDELAY"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ec.outer.cosmic_evaldelay = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_PC_FADCSUM_CH"))
		{
		  sscanf (str_tmp, "%*s 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X",
			  &vtpConf.pc.fadcsum_ch_en[0],  &vtpConf.pc.fadcsum_ch_en[1],
			  &vtpConf.pc.fadcsum_ch_en[2],  &vtpConf.pc.fadcsum_ch_en[3],
			  &vtpConf.pc.fadcsum_ch_en[4],  &vtpConf.pc.fadcsum_ch_en[5],
			  &vtpConf.pc.fadcsum_ch_en[6],  &vtpConf.pc.fadcsum_ch_en[7],
			  &vtpConf.pc.fadcsum_ch_en[8],  &vtpConf.pc.fadcsum_ch_en[9],
			  &vtpConf.pc.fadcsum_ch_en[10], &vtpConf.pc.fadcsum_ch_en[11],
			  &vtpConf.pc.fadcsum_ch_en[12], &vtpConf.pc.fadcsum_ch_en[13],
			  &vtpConf.pc.fadcsum_ch_en[14], &vtpConf.pc.fadcsum_ch_en[15]
			  );
		}
	      else if(!strcmp(keyword,"VTP_PC_COSMIC_EMIN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.pc.cosmic_emin = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_PC_COSMIC_MULTMAX"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.pc.cosmic_multmax = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_PC_COSMIC_HITWIDTH"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.pc.cosmic_hitwidth = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_PC_COSMIC_EVALDELAY"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.pc.cosmic_evaldelay = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_PC_COSMIC_PIXELEN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.pc.cosmic_pixelen = argi[0];
		}

	      else if(!strcmp(keyword,"VTP_HTCC_THRESHOLDS"))
		{
		  sscanf (str_tmp, "%*s %d %d %d", &argi[0], &argi[1], &argi[2]);
		  vtpConf.htcc.threshold[0] = argi[0];
		  vtpConf.htcc.threshold[1] = argi[1];
		  vtpConf.htcc.threshold[2] = argi[2];
		}
	      else if(!strcmp(keyword,"VTP_HTCC_NFRAMES"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.htcc.nframes = argi[0];
		}

	      else if(!strcmp(keyword,"VTP_CTOF_THRESHOLDS"))
		{
		  sscanf (str_tmp, "%*s %d %d %d", &argi[0], &argi[1], &argi[2]);
		  vtpConf.htcc.ctof_threshold[0] = argi[0];
		  vtpConf.htcc.ctof_threshold[1] = argi[1];
		  vtpConf.htcc.ctof_threshold[2] = argi[2];
		}
	      else if(!strcmp(keyword,"VTP_CTOF_NFRAMES"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.htcc.ctof_nframes = argi[0];
		}

	      else if(!strcmp(keyword,"VTP_FTOF_THRESHOLDS"))
		{
		  sscanf (str_tmp, "%*s %d %d %d", &argi[0], &argi[1], &argi[2]);
		  vtpConf.ftof.threshold[0] = argi[0];
		  vtpConf.ftof.threshold[1] = argi[1];
		  vtpConf.ftof.threshold[2] = argi[2];
		}
	      else if(!strcmp(keyword,"VTP_FTOF_NFRAMES"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ftof.nframes = argi[0];
		}

	      else if(!strcmp(keyword,"VTP_CND_THRESHOLDS"))
		{
		  sscanf (str_tmp, "%*s %d %d %d", &argi[0], &argi[1], &argi[2]);
		  vtpConf.cnd.threshold[0] = argi[0];
		  vtpConf.cnd.threshold[1] = argi[1];
		  vtpConf.cnd.threshold[2] = argi[2];
		}
	      else if(!strcmp(keyword,"VTP_CND_NFRAMES"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.cnd.nframes = argi[0];
		}

	      else if(!strcmp(keyword,"VTP_PCS_THRESHOLDS"))
		{
		  sscanf (str_tmp, "%*s %d %d %d", &argi[0], &argi[1], &argi[2]);
		  vtpConf.pcs.threshold[0] = argi[0];
		  vtpConf.pcs.threshold[1] = argi[1];
		  vtpConf.pcs.threshold[2] = argi[2];
		}
	      else if(!strcmp(keyword,"VTP_PCS_NFRAMES"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.pcs.nframes = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_PCS_DIPFACTOR"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.pcs.dipfactor = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_PCS_NSTRIP"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  vtpConf.pcs.nstrip_min = argi[0];
		  vtpConf.pcs.nstrip_max = argi[1];
		}
	      else if(!strcmp(keyword,"VTP_PCS_DALITZ"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  vtpConf.pcs.dalitz_min = argi[0];
		  vtpConf.pcs.dalitz_max = argi[1];
		}
	      else if(!strcmp(keyword,"VTP_PCS_COSMIC_EMIN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.pcs.cosmic_emin = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_PCS_COSMIC_MULTMAX"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.pcs.cosmic_multmax = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_PCS_COSMIC_HITWIDTH"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.pcs.cosmic_hitwidth = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_PCS_COSMIC_EVALDELAY"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.pcs.cosmic_evaldelay = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_PCS_COSMIC_PIXELEN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.pcs.cosmic_pixelen = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_PCU_THRESHOLDS"))
		{
		  sscanf (str_tmp, "%*s %d %d %d", &argi[0], &argi[1], &argi[2]);
		  vtpConf.pcs.pcu_threshold[0] = argi[0];
		  vtpConf.pcs.pcu_threshold[1] = argi[1];
		  vtpConf.pcs.pcu_threshold[2] = argi[2];
		}



	      else if(!strcmp(keyword,"VTP_ECS_FADCSUM_CH"))
		{
		  sscanf (str_tmp, "%*s 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X",
			  &vtpConf.ecs.fadcsum_ch_en[0],  &vtpConf.ecs.fadcsum_ch_en[1],
			  &vtpConf.ecs.fadcsum_ch_en[2],  &vtpConf.ecs.fadcsum_ch_en[3],
			  &vtpConf.ecs.fadcsum_ch_en[4],  &vtpConf.ecs.fadcsum_ch_en[5],
			  &vtpConf.ecs.fadcsum_ch_en[6],  &vtpConf.ecs.fadcsum_ch_en[7],
			  &vtpConf.ecs.fadcsum_ch_en[8],  &vtpConf.ecs.fadcsum_ch_en[9],
			  &vtpConf.ecs.fadcsum_ch_en[10], &vtpConf.ecs.fadcsum_ch_en[11],
			  &vtpConf.ecs.fadcsum_ch_en[12], &vtpConf.ecs.fadcsum_ch_en[13],
			  &vtpConf.ecs.fadcsum_ch_en[14], &vtpConf.ecs.fadcsum_ch_en[15]
			  );
		}
	      else if(!strcmp(keyword,"VTP_ECS_INNER_COSMIC_EMIN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ecs.inner.cosmic_emin = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_ECS_INNER_COSMIC_MULTMAX"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ecs.inner.cosmic_multmax = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_ECS_INNER_COSMIC_HITWIDTH"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ecs.inner.cosmic_hitwidth = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_ECS_INNER_COSMIC_EVALDELAY"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ecs.inner.cosmic_evaldelay = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_ECS_OUTER_COSMIC_EMIN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ecs.outer.cosmic_emin = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_ECS_OUTER_COSMIC_MULTMAX"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ecs.outer.cosmic_multmax = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_ECS_OUTER_COSMIC_HITWIDTH"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ecs.outer.cosmic_hitwidth = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_ECS_OUTER_COSMIC_EVALDELAY"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ecs.outer.cosmic_evaldelay = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_ECS_THRESHOLDS"))
		{
		  sscanf (str_tmp, "%*s %d %d %d", &argi[0], &argi[1], &argi[2]);
		  vtpConf.ecs.threshold[0] = argi[0];
		  vtpConf.ecs.threshold[1] = argi[1];
		  vtpConf.ecs.threshold[2] = argi[2];
		}
	      else if(!strcmp(keyword,"VTP_ECS_NFRAMES"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ecs.nframes = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_ECS_DIPFACTOR"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ecs.dipfactor = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_ECS_NSTRIP"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  vtpConf.ecs.nstrip_min = argi[0];
		  vtpConf.ecs.nstrip_max = argi[1];
		}
	      else if(!strcmp(keyword,"VTP_ECS_DALITZ"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  vtpConf.ecs.dalitz_min = argi[0];
		  vtpConf.ecs.dalitz_max = argi[1];
		}



	      else if(!strcmp(keyword,"VTP_GT_LATENCY"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.gt.trig_latency = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_GT_WIDTH"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.gt.trig_width = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_GT_TRG"))
		{
		  sscanf (str_tmp, "%*s %d", &trg_bit);
		  if(trg_bit<0 || trg_bit>=32)
		    {
		      printf("\nReadConfigFile: Wrong trg bit  number %d\n\n",trg_bit);
		      return(-4);
		    }
		}
	      else if(!strcmp(keyword,"VTP_GT_TRG_SSP_STRIGGER_MASK"))
		{
		  argc = sscanf (str_tmp, "%*s 0x%X 0x%X",&argi[0],&argi[1]);
		  if(trg_bit<0 || trg_bit>=32)
		    {
		      printf("\nReadConfigFile: Wrong trg bit  number %d\n\n",trg_bit);
		      return(-4);
		    }
		  vtpConf.gt.trgbits[trg_bit].ssp_strigger_bit_mask[0] = argi[0];
		  if(argc>=2) vtpConf.gt.trgbits[trg_bit].ssp_strigger_bit_mask[1] = argi[1];
		}
	      else if(!strcmp(keyword,"VTP_GT_TRG_SSP_CTRIGGER_MASK"))
		{
		  sscanf (str_tmp, "%*s 0x%X", &argi[0]);
		  if(trg_bit<0 || trg_bit>=32)
		    {
		      printf("\nReadConfigFile: Wrong trg bit  number %d\n\n",trg_bit);
		      return(-4);
		    }
		  vtpConf.gt.trgbits[trg_bit].ssp_ctrigger_bit_mask = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_GT_TRG_SSP_SECTOR_MASK"))
		{
		  argc = sscanf (str_tmp, "%*s 0x%X 0x%X",&argi[0],&argi[1]);
		  if(trg_bit<0 || trg_bit>=32)
		    {
		      printf("\nReadConfigFile: Wrong trg bit  number %d\n\n",trg_bit);
		      return(-4);
		    }
		  vtpConf.gt.trgbits[trg_bit].ssp_sector_mask[0] = argi[0];
		  if(argc>=2) vtpConf.gt.trgbits[trg_bit].ssp_sector_mask[1] = argi[1];
		}
	      else if(!strcmp(keyword,"VTP_GT_TRG_SSP_SECTOR_MULT_MIN"))
		{
		  argc = sscanf (str_tmp, "%*s %d %d", &argi[0],&argi[1]);
		  if(trg_bit<0 || trg_bit>=32)
		    {
		      printf("\nReadConfigFile: Wrong trg bit  number %d\n\n",trg_bit);
		      return(-4);
		    }
		  vtpConf.gt.trgbits[trg_bit].sector_mult_min[0] = argi[0];
		  if(argc>=2) vtpConf.gt.trgbits[trg_bit].sector_mult_min[1] = argi[1];
		}
	      else if(!strcmp(keyword,"VTP_GT_TRG_SSP_SECTOR_WIDTH"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  if(trg_bit<0 || trg_bit>=32)
		    {
		      printf("\nReadConfigFile: Wrong trg bit  number %d\n\n",trg_bit);
		      return(-4);
		    }
		  vtpConf.gt.trgbits[trg_bit].sector_coin_width = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_GT_TRG_PULSER_FREQ"))
		{
		  sscanf (str_tmp, "%*s %f", &argf[0]);
		  if(trg_bit<0 || trg_bit>=32)
		    {
		      printf("\nReadConfigFile: Wrong trg bit  number %d\n\n",trg_bit);
		      return(-4);
		    }
		  vtpConf.gt.trgbits[trg_bit].pulser_freq = argf[0];
		}
	      else if(!strcmp(keyword,"VTP_GT_TRG_DELAY"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  if(trg_bit<0 || trg_bit>=32)
		    {
		      printf("\nReadConfigFile: Wrong trg bit  number %d\n\n",trg_bit);
		      return(-4);
		    }
		  vtpConf.gt.trgbits[trg_bit].delay = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_GT_TRG_PRESCALE"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  if(trg_bit<0 || trg_bit>=32)
		    {
		      printf("\nReadConfigFile: Wrong trg bit  number %d\n\n",trg_bit);
		      return(-4);
		    }
		  vtpConf.gt.trgbits[trg_bit].prescale = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_GT_TRGBIT"))
		{
		  argc = sscanf (str_tmp, "%*s %d %d %d %d %d %d %d %d",&argi[0],&argi[1],&argi[2],&argi[3],&argi[4],&argi[5],&argi[6],&argi[7]);
		  if(argi[0]<0 || argi[0]>=32)
		    {
		      printf("\nReadConfigFile: Wrong trg bit  number %d\n\n",argi[0]);
		      return(-4);
		    }
		  vtpConf.gt.trgbits[argi[0]].ssp_strigger_bit_mask[0] = argi[1];
		  vtpConf.gt.trgbits[argi[0]].ssp_strigger_bit_mask[1] = 0;
		  vtpConf.gt.trgbits[argi[0]].ssp_sector_mask[0] = argi[2];
		  vtpConf.gt.trgbits[argi[0]].ssp_sector_mask[1] = 0;
		  vtpConf.gt.trgbits[argi[0]].sector_mult_min[0] = argi[3];
		  vtpConf.gt.trgbits[argi[0]].sector_mult_min[1] = 0;
		  vtpConf.gt.trgbits[argi[0]].sector_coin_width = argi[4];
		  vtpConf.gt.trgbits[argi[0]].ssp_ctrigger_bit_mask = argi[5];
		  if(argc>=7) vtpConf.gt.trgbits[argi[0]].delay = argi[6];
		  if(argc>=8) vtpConf.gt.trgbits[argi[0]].prescale = argi[7];
		}
	      else if(!strcmp(keyword,"VTP_GT_TRGBIT2"))
		{
		  argc = sscanf (str_tmp, "%*s %d %d %d %d %d %d %d %d %d %d %d",&argi[0],&argi[1],&argi[2],&argi[3],&argi[4],&argi[5],&argi[6],&argi[7],&argi[8],&argi[9],&argi[10]);
		  if(argi[0]<0 || argi[0]>=32)
		    {
		      printf("\nReadConfigFile: Wrong trg bit  number %d\n\n",argi[0]);
		      return(-4);
		    }
		  vtpConf.gt.trgbits[argi[0]].ssp_strigger_bit_mask[0] = argi[1];
		  vtpConf.gt.trgbits[argi[0]].ssp_sector_mask[0] = argi[2];
		  vtpConf.gt.trgbits[argi[0]].sector_mult_min[0] = argi[3];
		  vtpConf.gt.trgbits[argi[0]].ssp_strigger_bit_mask[1] = argi[4];
		  vtpConf.gt.trgbits[argi[0]].ssp_sector_mask[1] = argi[5];
		  vtpConf.gt.trgbits[argi[0]].sector_mult_min[1] = argi[6];
		  vtpConf.gt.trgbits[argi[0]].sector_coin_width = argi[7];
		  vtpConf.gt.trgbits[argi[0]].ssp_ctrigger_bit_mask = argi[8];
		  vtpConf.gt.trgbits[argi[0]].delay = argi[9];
		  vtpConf.gt.trgbits[argi[0]].prescale = argi[10];
		}

	      else if(!strcmp(keyword,"VTP_DC_SEGTHR"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  if(argi[0] < 0 || argi[0] > 2)
		    {
		      printf("\n%s: ERROR - invalid keyword %s instance %d\n", __func__, keyword, argi[0]);
		      return -1;
		    }
		  vtpConf.dc.dcsegfind_threshold[argi[0]] = argi[1];
		}
	      else if(!strcmp(keyword,"VTP_HCAL_HIT_DT"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.hcal.hit_dt = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_HCAL_HIT_EMIN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.hcal.cluster_emin = argi[0];
		}


	      else if(!strcmp(keyword,"VTP_FTCAL_FADCSUM_CH"))
		{
		  sscanf (str_tmp, "%*s 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X",
			  &vtpConf.ftcal.fadcsum_ch_en[0],  &vtpConf.ftcal.fadcsum_ch_en[1],
			  &vtpConf.ftcal.fadcsum_ch_en[2],  &vtpConf.ftcal.fadcsum_ch_en[3],
			  &vtpConf.ftcal.fadcsum_ch_en[4],  &vtpConf.ftcal.fadcsum_ch_en[5],
			  &vtpConf.ftcal.fadcsum_ch_en[6],  &vtpConf.ftcal.fadcsum_ch_en[7],
			  &vtpConf.ftcal.fadcsum_ch_en[8],  &vtpConf.ftcal.fadcsum_ch_en[9],
			  &vtpConf.ftcal.fadcsum_ch_en[10], &vtpConf.ftcal.fadcsum_ch_en[11],
			  &vtpConf.ftcal.fadcsum_ch_en[12], &vtpConf.ftcal.fadcsum_ch_en[13],
			  &vtpConf.ftcal.fadcsum_ch_en[14], &vtpConf.ftcal.fadcsum_ch_en[15]
			  );
		}
	      else if(!strcmp(keyword,"VTP_FTCAL_SEED_EMIN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ftcal.seed_emin = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_FTCAL_SEED_DT"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ftcal.seed_dt = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_FTCAL_HODO_DT"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ftcal.hodo_dt = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_FTHODO_EMIN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.fthodo.hit_emin = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_FTCAL_CLUSTER_DEADTIME_EMIN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ftcal.deadtime_emin = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_FTCAL_CLUSTER_DEADTIME"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.ftcal.deadtime = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_HPS_ECAL_TOP"))
		{
		  vtpConf.hps.cluster.top_nbottom = 1;
		}
	      else if(!strcmp(keyword,"VTP_HPS_ECAL_BOTTOM"))
		{
		  vtpConf.hps.cluster.top_nbottom = 0;
		}
	      else if(!strcmp(keyword,"VTP_HPS_ECAL_CLUSTER_HIT_DT"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.hps.cluster.hit_dt = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_HPS_ECAL_CLUSTER_SEED_THR"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.hps.cluster.seed_thr = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_HPS_HODOSCOPE_FADCHIT_THR"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.hps.hodoscope.fadchit_thr = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_HPS_HODOSCOPE_HODO_THR"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.hps.hodoscope.hodo_thr = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_HPS_HODOSCOPE_HODO_DT"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.hps.hodoscope.hit_dt = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_HPS_CALIB_HODOSCOPE_TOP_EN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.hps.calib.hodoscope_top_en = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_HPS_CALIB_HODOSCOPE_BOT_EN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.hps.calib.hodoscope_bot_en = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_HPS_CALIB_COSMIC_DT"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.hps.calib.cosmic_dt = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_HPS_CALIB_COSMIC_TOP_EN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.hps.calib.cosmic_top_en = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_HPS_CALIB_COSMIC_BOT_EN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.hps.calib.cosmic_bot_en = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_HPS_CALIB_PULSER_EN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.hps.calib.pulser_en = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_HPS_CALIB_PULSER_FREQ"))
		{
		  sscanf (str_tmp, "%*s %f", &argf[0]);
		  vtpConf.hps.calib.pulser_freq = argf[0];
		}
	      else if(!strcmp(keyword,"VTP_HPS_SINGLE_EMIN"))
		{
		  sscanf (str_tmp, "%*s %d %d %d", &argi[0], &argi[1], &argi[2]);
		  if(argi[0]<0 || argi[0]>=4)
		    {
		      printf("\nReadConfigFile: Wrong singles bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.single_trig[argi[0]].cluster_emin = argi[1];
		  vtpConf.hps.single_trig[argi[0]].cluster_emin_en = argi[2];
		}
	      else if(!strcmp(keyword,"VTP_HPS_SINGLE_EMAX"))
		{
		  sscanf (str_tmp, "%*s %d %d %d", &argi[0], &argi[1], &argi[2]);
		  if(argi[0]<0 || argi[0]>=4)
		    {
		      printf("\nReadConfigFile: Wrong singles bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.single_trig[argi[0]].cluster_emax = argi[1];
		  vtpConf.hps.single_trig[argi[0]].cluster_emax_en = argi[2];
		}
	      else if(!strcmp(keyword,"VTP_HPS_SINGLE_NMIN"))
		{
		  sscanf (str_tmp, "%*s %d %d %d", &argi[0], &argi[1], &argi[2]);
		  if(argi[0]<0 || argi[0]>=4)
		    {
		      printf("\nReadConfigFile: Wrong singles bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.single_trig[argi[0]].cluster_nmin = argi[1];
		  vtpConf.hps.single_trig[argi[0]].cluster_nmin_en = argi[2];
		}
	      else if(!strcmp(keyword,"VTP_HPS_SINGLE_XMIN"))
		{
		  sscanf (str_tmp, "%*s %d %d %d", &argi[0], &argi[1], &argi[2]);
		  if(argi[0]<0 || argi[0]>=4)
		    {
		      printf("\nReadConfigFile: Wrong singles bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.single_trig[argi[0]].cluster_xmin = argi[1];
		  vtpConf.hps.single_trig[argi[0]].cluster_xmin_en = argi[2];
		}
	      else if(!strcmp(keyword,"VTP_HPS_SINGLE_PDE"))
		{
		  sscanf (str_tmp, "%*s %d %f %f %f %f %d", &argi[0], &argf[0], &argf[1], &argf[2], &argf[3], &argi[1]);
		  if(argi[0]<0 || argi[0]>=4)
		    {
		      printf("\nReadConfigFile: Wrong singles bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.single_trig[argi[0]].pde_c[0] = argf[0];
		  vtpConf.hps.single_trig[argi[0]].pde_c[1] = argf[1];
		  vtpConf.hps.single_trig[argi[0]].pde_c[2] = argf[2];
		  vtpConf.hps.single_trig[argi[0]].pde_c[3] = argf[3];
		  vtpConf.hps.single_trig[argi[0]].pde_en   = argi[1];
		}
	      else if(!strcmp(keyword,"VTP_HPS_SINGLE_HODO"))
		{
		  sscanf (str_tmp, "%*s %d %d %d %d %d", &argi[0], &argi[1], &argi[2], &argi[3], &argi[4]);
		  if(argi[0]<0 || argi[0]>=4)
		    {
		      printf("\nReadConfigFile: Wrong singles bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.single_trig[argi[0]].hodo_l1_en = argi[1];
		  vtpConf.hps.single_trig[argi[0]].hodo_l2_en = argi[2];
		  vtpConf.hps.single_trig[argi[0]].hodo_l1l2_geom_en = argi[3];
		  vtpConf.hps.single_trig[argi[0]].hodo_l1l2x_geom_en = argi[4];
		}
	      else if(!strcmp(keyword,"VTP_HPS_SINGLE_EN"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  if(argi[0]<0 || argi[0]>=4)
		    {
		      printf("\nReadConfigFile: Wrong singles bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.single_trig[argi[0]].en = argi[1];
		}
	      else if(!strcmp(keyword,"VTP_HPS_PAIR_EMIN"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  if(argi[0]<0 || argi[0]>=4)
		    {
		      printf("\nReadConfigFile: Wrong pair bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.pair_trig[argi[0]].cluster_emin = argi[1];
		}
	      else if(!strcmp(keyword,"VTP_HPS_PAIR_EMAX"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  if(argi[0]<0 || argi[0]>=4)
		    {
		      printf("\nReadConfigFile: Wrong singles bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.pair_trig[argi[0]].cluster_emax = argi[1];
		}
	      else if(!strcmp(keyword,"VTP_HPS_PAIR_NMIN"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  if(argi[0]<0 || argi[0]>=4)
		    {
		      printf("\nReadConfigFile: Wrong pair bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.pair_trig[argi[0]].cluster_nmin = argi[1];
		}
	      else if(!strcmp(keyword,"VTP_HPS_PAIR_TIMECOINCIDENCE"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  if(argi[0]<0 || argi[0]>=4)
		    {
		      printf("\nReadConfigFile: Wrong pair bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.pair_trig[argi[0]].pair_dt = argi[1];
		}
	      else if(!strcmp(keyword,"VTP_HPS_PAIR_SUMMAX_MIN"))
		{
		  sscanf (str_tmp, "%*s %d %d %d %d", &argi[0], &argi[1], &argi[2], &argi[3]);
		  if(argi[0]<0 || argi[0]>=4)
		    {
		      printf("\nReadConfigFile: Wrong pair bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.pair_trig[argi[0]].pair_esum_max = argi[1];
		  vtpConf.hps.pair_trig[argi[0]].pair_esum_min = argi[2];
		  vtpConf.hps.pair_trig[argi[0]].pair_esum_en  = argi[3];
		}
	      else if(!strcmp(keyword,"VTP_HPS_PAIR_DIFFMAX"))
		{
		  sscanf (str_tmp, "%*s %d %d %d", &argi[0], &argi[1], &argi[2]);
		  if(argi[0]<0 || argi[0]>=4)
		    {
		      printf("\nReadConfigFile: Wrong pair bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.pair_trig[argi[0]].pair_ediff_max = argi[1];
		  vtpConf.hps.pair_trig[argi[0]].pair_ediff_en  = argi[2];
		}
	      else if(!strcmp(keyword,"VTP_HPS_PAIR_ENERGYDIST"))
		{
		  sscanf (str_tmp, "%*s %d %f %d %d", &argi[0], &argf[0], &argi[1], &argi[2]);
		  if(argi[0]<0 || argi[0]>=4)
		    {
		      printf("\nReadConfigFile: Wrong pair bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.pair_trig[argi[0]].pair_ed_factor = argf[0];
		  vtpConf.hps.pair_trig[argi[0]].pair_ed_thr = argi[1];
		  vtpConf.hps.pair_trig[argi[0]].pair_ed_en  = argi[2];
		}
	      else if(!strcmp(keyword,"VTP_HPS_PAIR_COPLANARITY"))
		{
		  sscanf (str_tmp, "%*s %d %d %d", &argi[0], &argi[1], &argi[2]);
		  if(argi[0]<0 || argi[0]>=4)
		    {
		      printf("\nReadConfigFile: Wrong pair bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.pair_trig[argi[0]].pair_coplanarity_tol = argi[1];
		  vtpConf.hps.pair_trig[argi[0]].pair_coplanarity_en  = argi[2];
		}
	      else if(!strcmp(keyword,"VTP_HPS_PAIR_HODO"))
		{
		  sscanf (str_tmp, "%*s %d %d %d %d %d", &argi[0], &argi[1], &argi[2], &argi[3], &argi[4]);
		  if(argi[0]<0 || argi[0]>=4)
		    {
		      printf("\nReadConfigFile: Wrong singles bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.pair_trig[argi[0]].hodo_l1_en = argi[1];
		  vtpConf.hps.pair_trig[argi[0]].hodo_l2_en = argi[2];
		  vtpConf.hps.pair_trig[argi[0]].hodo_l1l2_geom_en = argi[3];
		  vtpConf.hps.pair_trig[argi[0]].hodo_l1l2x_geom_en = argi[4];
		}
	      else if(!strcmp(keyword,"VTP_HPS_PAIR_EN"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  if(argi[0]<0 || argi[0]>=4)
		    {
		      printf("\nReadConfigFile: Wrong pair bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.pair_trig[argi[0]].en = argi[1];
		}
	      else if(!strcmp(keyword,"VTP_HPS_MULT_EMIN"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  if(argi[0]<0 || argi[0]>=2)
		    {
		      printf("\nReadConfigFile: Wrong multiplicity bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.mult_trig[argi[0]].cluster_emin = argi[1];
		}
	      else if(!strcmp(keyword,"VTP_HPS_MULT_EMAX"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  if(argi[0]<0 || argi[0]>=2)
		    {
		      printf("\nReadConfigFile: Wrong multiplicity bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.mult_trig[argi[0]].cluster_emax = argi[1];
		}
	      else if(!strcmp(keyword,"VTP_HPS_MULT_NMIN"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  if(argi[0]<0 || argi[0]>=2)
		    {
		      printf("\nReadConfigFile: Wrong multiplicity bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.mult_trig[argi[0]].cluster_nmin = argi[1];
		}
	      else if(!strcmp(keyword,"VTP_HPS_MULT_MIN"))
		{
		  sscanf (str_tmp, "%*s %d %d %d %d", &argi[0], &argi[1], &argi[2], &argi[3]);
		  if(argi[0]<0 || argi[0]>=2)
		    {
		      printf("\nReadConfigFile: Wrong multiplicity bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.mult_trig[argi[0]].mult_top_min = argi[1];
		  vtpConf.hps.mult_trig[argi[0]].mult_bot_min = argi[2];
		  vtpConf.hps.mult_trig[argi[0]].mult_tot_min = argi[3];
		}
	      else if(!strcmp(keyword,"VTP_HPS_MULT_DT"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  if(argi[0]<0 || argi[0]>=2)
		    {
		      printf("\nReadConfigFile: Wrong multiplicity bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.mult_trig[argi[0]].mult_dt = argi[1];
		}
	      else if(!strcmp(keyword,"VTP_HPS_MULT_EN"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  if(argi[0]<0 || argi[0]>=2)
		    {
		      printf("\nReadConfigFile: Wrong multiplicity bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.mult_trig[argi[0]].en = argi[1];
		}
	      else if(!strcmp(keyword,"VTP_HPS_FEE_EN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.hps.fee_trig.en = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_HPS_FEE_PRESCALE"))
		{
		  sscanf (str_tmp, "%*s %d %d %d %d", &argi[0], &argi[1], &argi[2], &argi[3]);
		  if(argi[0]<0 || argi[0]>6)
		    {
		      printf("\nReadConfigFile: Wrong FEE region %d\n\n",argi[0]);
		      return(-4);
		    }
		  vtpConf.hps.fee_trig.prescale_xmin[argi[0]] = argi[1];
		  vtpConf.hps.fee_trig.prescale_xmax[argi[0]] = argi[2];
		  vtpConf.hps.fee_trig.prescale[argi[0]]      = argi[3];
		}
	      else if(!strcmp(keyword,"VTP_HPS_FEE_EMIN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.hps.fee_trig.cluster_emin = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_HPS_FEE_EMAX"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.hps.fee_trig.cluster_emax = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_HPS_FEE_NMIN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.hps.fee_trig.cluster_nmin = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_HPS_LATENCY"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.hps.trig.latency = argi[0];
		}
	      else if(!strcmp(keyword,"VTP_HPS_PRESCALE"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  if(argi[0]<0 || argi[0]>=32)
		    {
		      printf("\nReadConfigFile: Wrong trg bit  number %d\n\n",argi[0]);
		      return(-4);
		    }

		  vtpConf.hps.trig.prescale[argi[0]] = argi[1];
		}


	      // FADC STREAMING PARAMETERS
        else if(!strcmp(keyword,"VTP_STREAMING_ROCID"))
        {
          sscanf (str_tmp, "%*s %d", &argi[0]);
          vtpConf.fadc_streaming.roc_id = argi[0];
	}
        else if(!strcmp(keyword,"VTP_STREAMING_NFRAME_BUF"))
        {
          sscanf (str_tmp, "%*s %d", &argi[0]);
          vtpConf.fadc_streaming.nframe_buf = argi[0];
        }
        else if(!strcmp(keyword,"VTP_STREAMING_FRAMELEN"))
        {
          sscanf (str_tmp, "%*s %d", &argi[0]);
          vtpConf.fadc_streaming.frame_len = argi[0];
        }
        else if(!strcmp(keyword,"VTP_STREAMING"))
        {
          sscanf (str_tmp, "%*s %d", &argi[0]);
          if(argi[0]<0 || argi[0]>1)
          {
            printf("\nReadConfigFile: Wrong Streaming EB index %d\n\n",argi[0]);
            return(-4);
          }
          streaming_eb = argi[0];
        }
        else if(!strcmp(keyword,"VTP_STREAMING_SLOT_EN"))
        {
          GET_READ_MSK8;
          vtpConf.fadc_streaming.eb[streaming_eb].mask_en = ui1;
        }
        else if(!strcmp(keyword,"VTP_STREAMING_NSTREAMS"))
        {
          sscanf (str_tmp, "%*s %d", &argi[0]);
          vtpConf.fadc_streaming.eb[streaming_eb].nstreams = argi[0];
        }
        else if(!strcmp(keyword,"VTP_STREAMING_CONNECT"))
        {
          sscanf (str_tmp, "%*s %d", &argi[0]);
          if(argi[0] > 0)
            vtpConf.fadc_streaming.eb[streaming_eb].connect = 0;
          else
            vtpConf.fadc_streaming.eb[streaming_eb].connect = 1;
        }
        else if(!strcmp(keyword,"VTP_STREAMING_IPADDR"))
        {
          sscanf (str_tmp, "%*s %d %d %d %d", &argi[0], &argi[1], &argi[2], &argi[3]);
          vtpConf.fadc_streaming.eb[streaming_eb].ipaddr[0] = argi[0];
          vtpConf.fadc_streaming.eb[streaming_eb].ipaddr[1] = argi[1];
          vtpConf.fadc_streaming.eb[streaming_eb].ipaddr[2] = argi[2];
          vtpConf.fadc_streaming.eb[streaming_eb].ipaddr[3] = argi[3];
        }
        else if(!strcmp(keyword,"VTP_STREAMING_SUBNET"))
        {
          sscanf (str_tmp, "%*s %d %d %d %d", &argi[0], &argi[1], &argi[2], &argi[3]);
          vtpConf.fadc_streaming.eb[streaming_eb].subnet[0] = argi[0];
          vtpConf.fadc_streaming.eb[streaming_eb].subnet[1] = argi[1];
          vtpConf.fadc_streaming.eb[streaming_eb].subnet[2] = argi[2];
          vtpConf.fadc_streaming.eb[streaming_eb].subnet[3] = argi[3];
        }
        else if(!strcmp(keyword,"VTP_STREAMING_GATEWAY"))
        {
          sscanf (str_tmp, "%*s %d %d %d %d", &argi[0], &argi[1], &argi[2], &argi[3]);
          vtpConf.fadc_streaming.eb[streaming_eb].gateway[0] = argi[0];
          vtpConf.fadc_streaming.eb[streaming_eb].gateway[1] = argi[1];
          vtpConf.fadc_streaming.eb[streaming_eb].gateway[2] = argi[2];
          vtpConf.fadc_streaming.eb[streaming_eb].gateway[3] = argi[3];
        }
        else if(!strcmp(keyword,"VTP_STREAMING_MAC"))
        {
          sscanf (str_tmp, "%*s 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X", &argi[0], &argi[1], &argi[2], &argi[3], &argi[4], &argi[5]);
          vtpConf.fadc_streaming.eb[streaming_eb].mac[0] = argi[0];
          vtpConf.fadc_streaming.eb[streaming_eb].mac[1] = argi[1];
          vtpConf.fadc_streaming.eb[streaming_eb].mac[2] = argi[2];
          vtpConf.fadc_streaming.eb[streaming_eb].mac[3] = argi[3];
          vtpConf.fadc_streaming.eb[streaming_eb].mac[4] = argi[4];
          vtpConf.fadc_streaming.eb[streaming_eb].mac[5] = argi[5];
        }
        else if(!strcmp(keyword,"VTP_STREAMING_DESTIP"))
        {
          sscanf (str_tmp, "%*s %d %d %d %d", &argi[0], &argi[1], &argi[2], &argi[3]);
          vtpConf.fadc_streaming.eb[streaming_eb].destip[0] = argi[0];
          vtpConf.fadc_streaming.eb[streaming_eb].destip[1] = argi[1];
          vtpConf.fadc_streaming.eb[streaming_eb].destip[2] = argi[2];
          vtpConf.fadc_streaming.eb[streaming_eb].destip[3] = argi[3];
        }
        else if(!strcmp(keyword,"VTP_STREAMING_DESTIPPORT"))
        {
          sscanf (str_tmp, "%*s %d", &argi[0]);
          vtpConf.fadc_streaming.eb[streaming_eb].destipport = argi[0];
        }
        else if(!strcmp(keyword,"VTP_STREAMING_LOCALPORT"))
        {
          sscanf (str_tmp, "%*s %d", &argi[0]);
          vtpConf.fadc_streaming.eb[streaming_eb].localport = argi[0];
        }

	      // VTP ROC CONFIG PARAMETERS
        else if(!strcmp(keyword,"VTP_ROC_ROCID"))
        {
          sscanf (str_tmp, "%*s %d", &argi[0]);
          vtpConf.vtp_roc.roc_id = argi[0];
	}
        else if(!strcmp(keyword,"VTP_ROC_DEST_IP"))
        {
          sscanf (str_tmp, "%*s %x", &argi[0]);
          vtpConf.vtp_roc.destip = argi[0];
        }
        else if(!strcmp(keyword,"VTP_ROC_DEST_PORT"))
        {
          sscanf (str_tmp, "%*s %d", &argi[0]);
          vtpConf.vtp_roc.destipport = argi[0];
        }	      


	      // COMPTON CONFIG PARAMETERS
        else if(!strcmp(keyword,"VTP_COMPTON_VETROC_WIDTH"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.compton.vetroc_width = argi[0];
		}
        else if(!strcmp(keyword,"VTP_COMPTON_LATENCY"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.compton.trig.latency = argi[0];
		}
        else if(!strcmp(keyword,"VTP_COMPTON_WIDTH"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  vtpConf.compton.trig.width = argi[0];
		}

        else if(!strcmp(keyword,"VTP_COMPTON_FADC_THRESHOLD"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  if(argi[0]<0 || argi[0]>=5)
		  {
		      printf("\nReadConfigFile: Wrong compton instannce %d\n\n",argi[0]);
		      return(-4);
		  }
printf("Set FADC_THRESHOLD: %d %d\n", argi[0], argi[1]);
		  vtpConf.compton.fadc_threshold[argi[0]] = argi[1];
		}
        else if(!strcmp(keyword,"VTP_COMPTON_FADC_EN_MASK"))
		{
		  args = sscanf (str_tmp, "%*s %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
			  &argi[0],
		      &msk[ 0], &msk[ 1], &msk[ 2], &msk[ 3],
		      &msk[ 4], &msk[ 5], &msk[ 6], &msk[ 7],
		      &msk[ 8], &msk[ 9], &msk[10], &msk[11],
		      &msk[12], &msk[13], &msk[14], &msk[15]);
		  if(argi[0]<0 || argi[0]>=5)
		  {
		      printf("\nReadConfigFile: Wrong compton instannce %d\n\n",argi[0]);
		      return(-4);
		  }

  		  ui1 = 0;
          for(jj=0; jj<16; jj++)
    	  {
            if((msk[jj] < 0) || (msk[jj] > 1))
	        {
  	          printf("\nReadConfigFile: Wrong mask bit value, %d\n\n",msk[jj]);
			  return(-6);
	        }
      		ui1 |= (msk[jj]<<jj);
    	  }
		  vtpConf.compton.fadc_mask[argi[0]] = ui1;
printf("Set FADC_MASK: %d %04X\n", argi[0], vtpConf.compton.fadc_mask[argi[0]]);
		}
        else if(!strcmp(keyword,"VTP_COMPTON_EPLANE_MULT_MIN"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  if(argi[0]<0 || argi[0]>=5)
		  {
		      printf("\nReadConfigFile: Wrong compton instannce %d\n\n",argi[0]);
		      return(-4);
		  }
printf("Set EPLANE_MULT_MIN: %d %d\n", argi[0], argi[1]);
          vtpConf.compton.eplane_mult_min[argi[0]] = argi[1];
		}
        else if(!strcmp(keyword,"VTP_COMPTON_EPLANE_MASK"))
		{
		  sscanf (str_tmp, "%*s %d %d %d %d %d", &argi[0], &argi[1], &argi[2], &argi[3], &argi[4]);
		  if(argi[0]<0 || argi[0]>=5)
		  {
		      printf("\nReadConfigFile: Wrong compton instannce %d\n\n",argi[0]);
		      return(-4);
		  }
          vtpConf.compton.eplane_mask[argi[0]] = 0;
		  if(argi[1]==1) vtpConf.compton.eplane_mask[argi[0]] |= 0x1;
		  if(argi[2]==1) vtpConf.compton.eplane_mask[argi[0]] |= 0x2;
		  if(argi[3]==1) vtpConf.compton.eplane_mask[argi[0]] |= 0x4;
		  if(argi[4]==1) vtpConf.compton.eplane_mask[argi[0]] |= 0x8;
printf("Set EPLANE_MASK: %d %d\n", argi[0], vtpConf.compton.eplane_mask[argi[0]]);
		}
        else if(!strcmp(keyword,"VTP_COMPTON_PRESCALE"))
		{
		  sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
		  if(argi[0]<0 || argi[0]>=32)
		    {
		      printf("\nReadConfigFile: Wrong trg bit  number %d\n\n",argi[0]);
		      return(-4);
		    }
printf("Set PRESCALE: %d %d\n", argi[0], argi[1]);
		  vtpConf.compton.trig.prescale[argi[0]] = argi[1];
		}
        else if(!strcmp(keyword,"VTP_COMPTON_SCALER_READOUT_EN"))
		{
		  sscanf (str_tmp, "%*s %d", &argi[0]);
		  if(argi[0] > 0)
	                  vtpConf.compton.enable_scaler_readout = 1;
		  else
		    vtpConf.compton.enable_scaler_readout = 0;
		}
        else if(!strcmp(keyword,"VTP_COMPTON_DELAY"))
	  {
	    sscanf (str_tmp, "%*s %d %d", &argi[0], &argi[1]);
	    if(argi[0]<0 || argi[0]>=32)
	      {
		printf("\nReadConfigFile: Wrong trg bit  number %d\n\n",argi[0]);
		return(-4);
	      }
	    printf("Set DELAY: %d %d\n", argi[0], argi[1]);
	    vtpConf.compton.trig.delay[argi[0]] = argi[1];
	  }
        else
	  {
	    printf("Error: VTP unknown line: fgets returns %s so keyword=%s\n\n",str_tmp,keyword);
	  }
	    }
	}
      fclose(fd);
    }

  return(0);
  args = args; // dump the unused variable warnings, if we're not checking this return value.
}

int
vtpUploadAllPrint()
{
  char str[16001];
  vtpUploadAll(str, 16000);
  printf("%s",str);
  return 0;
}

/* download setting into VTP */
int
vtpDownloadAll()
{
  int enable_flags;
  int ii, inst;

#ifdef VTPDOWNLOADALL_FIRMWARE_LOAD
  char fname[FNLEN];


  // Open VTP hardware interfaces
  vtpOpen(VTP_FPGA_OPEN|VTP_I2C_OPEN|VTP_SPI_OPEN);

  // Load VTP Zynq FPGA image
  if(strlen(vtpConf.fw_filename_z7) <= 0)
  {
    printf("No Z7 firmware file specified. Firmware is expected to loaded already.\n");
    return(0);
  }else{
    snprintf(fname, FNLEN, "%s/firmwares/%s", clonparms, vtpConf.fw_filename_z7);
    if(vtpZ7CfgLoad(fname) != OK)
      exit(1);
  }

  // Load VTP V7 FPGA image
  if(strlen(vtpConf.fw_filename_v7) <= 0)
  {
    printf("No V7 firmware file specified. Firmware is expected to loaded already.\n");
    return(0);
  }else{
    snprintf(fname, FNLEN, "%s/firmwares/%s", clonparms, vtpConf.fw_filename_v7);
    if(vtpV7CfgLoad(fname) != OK)
      exit(1);
  }


  if(vtpConf.refclk == 125)
    vtpInit(VTP_INIT_CLK_VXS_125);
  else if(vtpConf.refclk == 250)
    vtpInit(VTP_INIT_CLK_VXS_250);
  else
    {
      printf("ERROR - unknown reference clock specified %d\n",vtpConf.refclk);
      exit(1);
    }

#endif

  // Get firmware info for both V7 and Z7 
  for(ii=0;ii<2;ii++) {
    vtpConf.fw_rev[ii] = vtpGetFW_Version(ii);
    vtpConf.fw_type[ii] = vtpGetFW_Type(ii);
  }
 
  printf("%s: V7 Chip vtpConfig type = %d\n",
	 __func__, vtpConf.fw_type[0]);
  printf("%s: Z7 Chip vtpConfig type = %d\n",
	 __func__, vtpConf.fw_type[1]);


  // Set parameters based on firmware type
  vtpSetWindow(vtpConf.window_offset, vtpConf.window_width);

  vtpEnableTriggerPayloadMask(vtpConf.payload_en);
  vtpEnableTriggerFiberMask(vtpConf.fiber_en);

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_EC)
    {
      // EC configuration
      vtpSetFadcSum_MaskEn(vtpConf.ec.fadcsum_ch_en);

      vtpSetECtrig_emin(0, vtpConf.ec.inner.hit_emin);
      vtpSetECtrig_dt(0, vtpConf.ec.inner.hit_dt);
      vtpSetECtrig_dalitz(0, vtpConf.ec.inner.dalitz_min, vtpConf.ec.inner.dalitz_max);
      vtpSetECcosmic_emin(0, vtpConf.ec.inner.cosmic_emin);
      vtpSetECcosmic_multmax(0, vtpConf.ec.inner.cosmic_multmax);
      vtpSetECcosmic_width(0, vtpConf.ec.inner.cosmic_hitwidth);
      vtpSetECcosmic_delay(0, vtpConf.ec.inner.cosmic_evaldelay);

      vtpSetECtrig_emin(1, vtpConf.ec.outer.hit_emin);
      vtpSetECtrig_dt(1, vtpConf.ec.outer.hit_dt);
      vtpSetECtrig_dalitz(1, vtpConf.ec.outer.dalitz_min, vtpConf.ec.outer.dalitz_max);
      vtpSetECcosmic_emin(1, vtpConf.ec.outer.cosmic_emin);
      vtpSetECcosmic_multmax(1, vtpConf.ec.outer.cosmic_multmax);
      vtpSetECcosmic_width(1, vtpConf.ec.outer.cosmic_hitwidth);
      vtpSetECcosmic_delay(1, vtpConf.ec.outer.cosmic_evaldelay);
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_PC)
    {
      // PC configuration
      vtpSetFadcSum_MaskEn(vtpConf.pc.fadcsum_ch_en);

      vtpSetPCcosmic_emin(vtpConf.pc.cosmic_emin);
      vtpSetPCcosmic_multmax(vtpConf.pc.cosmic_multmax);
      vtpSetPCcosmic_width(vtpConf.pc.cosmic_hitwidth);
      vtpSetPCcosmic_delay(vtpConf.pc.cosmic_evaldelay);
      vtpSetPCcosmic_pixel(vtpConf.pc.cosmic_pixelen);
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_HTCC)
    {
      // HTCC configuration
      vtpSetHTCC_thresholds(vtpConf.htcc.threshold[0], vtpConf.htcc.threshold[1], vtpConf.htcc.threshold[2]);
      vtpSetHTCC_nframes(vtpConf.htcc.nframes);

      vtpSetCTOF_thresholds(vtpConf.htcc.ctof_threshold[0], vtpConf.htcc.ctof_threshold[1], vtpConf.htcc.ctof_threshold[2]);
      vtpSetCTOF_nframes(vtpConf.htcc.ctof_nframes);
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_FTOF)
    {
      // FTOF configuration
      vtpSetFTOF_thresholds(vtpConf.ftof.threshold[0], vtpConf.ftof.threshold[1], vtpConf.ftof.threshold[2]);
      vtpSetFTOF_nframes(vtpConf.ftof.nframes);
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_CND)
    {
      // CND configuration
      vtpSetCND_thresholds(vtpConf.cnd.threshold[0], vtpConf.cnd.threshold[1], vtpConf.cnd.threshold[2]);
      vtpSetCND_nframes(vtpConf.cnd.nframes);
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_PCS)
    {
      // PCS configuration
      vtpSetPCS_thresholds(vtpConf.pcs.threshold[0], vtpConf.pcs.threshold[1], vtpConf.pcs.threshold[2]);
      vtpSetPCS_nframes(vtpConf.pcs.nframes);
      vtpSetPCS_dipfactor(vtpConf.pcs.dipfactor);
      vtpSetPCS_nstrip(vtpConf.pcs.nstrip_min, vtpConf.pcs.nstrip_max);
      vtpSetPCS_dalitz(vtpConf.pcs.dalitz_min, vtpConf.pcs.dalitz_max);
      vtpSetPCU_thresholds(vtpConf.pcs.pcu_threshold[0], vtpConf.pcs.pcu_threshold[1], vtpConf.pcs.pcu_threshold[2]);
      vtpSetPCcosmic_emin(vtpConf.pcs.cosmic_emin);
      vtpSetPCcosmic_multmax(vtpConf.pcs.cosmic_multmax);
      vtpSetPCcosmic_width(vtpConf.pcs.cosmic_hitwidth);
      vtpSetPCcosmic_delay(vtpConf.pcs.cosmic_evaldelay);
      vtpSetPCcosmic_pixel(vtpConf.pcs.cosmic_pixelen);
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_ECS)
    {
      // ECS configuration
      vtpSetFadcSum_MaskEn(vtpConf.ecs.fadcsum_ch_en);

      vtpSetECS_thresholds(vtpConf.ecs.threshold[0], vtpConf.ecs.threshold[1], vtpConf.ecs.threshold[2]);
      vtpSetECS_nframes(vtpConf.ecs.nframes);
      vtpSetECS_dipfactor(vtpConf.ecs.dipfactor);
      vtpSetECS_nstrip(vtpConf.ecs.nstrip_min, vtpConf.ecs.nstrip_max);
      vtpSetECS_dalitz(vtpConf.ecs.dalitz_min, vtpConf.ecs.dalitz_max);


      vtpSetECcosmic_emin(0, vtpConf.ecs.inner.cosmic_emin);
      vtpSetECcosmic_multmax(0, vtpConf.ecs.inner.cosmic_multmax);
      vtpSetECcosmic_width(0, vtpConf.ecs.inner.cosmic_hitwidth);
      vtpSetECcosmic_delay(0, vtpConf.ecs.inner.cosmic_evaldelay);

      vtpSetECcosmic_emin(1, vtpConf.ecs.outer.cosmic_emin);
      vtpSetECcosmic_multmax(1, vtpConf.ecs.outer.cosmic_multmax);
      vtpSetECcosmic_width(1, vtpConf.ecs.outer.cosmic_hitwidth);
      vtpSetECcosmic_delay(1, vtpConf.ecs.outer.cosmic_evaldelay);
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_GT)
    {
      // GT configuration
      vtpSetGt_latency(vtpConf.gt.trig_latency);
      vtpSetGt_width(vtpConf.gt.trig_width);

      for(ii=0; ii<32; ii++)
	{
	  vtpSetGtTriggerBit(ii,
			     vtpConf.gt.trgbits[ii].ssp_strigger_bit_mask[0],
			     vtpConf.gt.trgbits[ii].ssp_sector_mask[0],
			     vtpConf.gt.trgbits[ii].sector_mult_min[0],
			     vtpConf.gt.trgbits[ii].ssp_strigger_bit_mask[1],
			     vtpConf.gt.trgbits[ii].ssp_sector_mask[1],
			     vtpConf.gt.trgbits[ii].sector_mult_min[1],
			     vtpConf.gt.trgbits[ii].sector_coin_width,
			     vtpConf.gt.trgbits[ii].ssp_ctrigger_bit_mask,
			     vtpConf.gt.trgbits[ii].delay,
			     vtpConf.gt.trgbits[ii].pulser_freq,
			     vtpConf.gt.trgbits[ii].prescale
			     );
	}
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_DC)
    {
      vtpSetDc_SegmentThresholdMin(0, vtpConf.dc.dcsegfind_threshold[0]);
      vtpSetDc_SegmentThresholdMin(1, vtpConf.dc.dcsegfind_threshold[1]);
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_HCAL)
    {
      vtpSetHcal_ClusterCoincidence(vtpConf.hcal.hit_dt);
      vtpSetHcal_ClusterThreshold(vtpConf.hcal.cluster_emin);
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_FTCAL)
    {
      vtpSetFadcSum_MaskEn(vtpConf.ftcal.fadcsum_ch_en);
      vtpSetFTCALseed_emin(vtpConf.ftcal.seed_emin);
      vtpSetFTCALseed_dt(vtpConf.ftcal.seed_dt);
      vtpSetFTCALhodo_dt(vtpConf.ftcal.hodo_dt);
      vtpSetFTCALcluster_deadtime(vtpConf.ftcal.deadtime);
      vtpSetFTCALcluster_deadtime_emin(vtpConf.ftcal.deadtime_emin);
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_FTHODO)
    {
      vtpSetFTHODOemin(vtpConf.fthodo.hit_emin);
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_HPS)
    {
      vtpSetHPS_Cluster(vtpConf.hps.cluster.top_nbottom, vtpConf.hps.cluster.hit_dt, vtpConf.hps.cluster.seed_thr);
      vtpSetHPS_Hodoscope(vtpConf.hps.hodoscope.hit_dt, vtpConf.hps.hodoscope.fadchit_thr, vtpConf.hps.hodoscope.hodo_thr);

      for(ii=0;ii<4;ii++)
	{
	  enable_flags = 0;
	  enable_flags |= vtpConf.hps.single_trig[ii].cluster_emin_en    ? (1<< 0) : 0;
	  enable_flags |= vtpConf.hps.single_trig[ii].cluster_emax_en    ? (1<< 1) : 0;
	  enable_flags |= vtpConf.hps.single_trig[ii].cluster_nmin_en    ? (1<< 2) : 0;
	  enable_flags |= vtpConf.hps.single_trig[ii].cluster_xmin_en    ? (1<< 3) : 0;
	  enable_flags |= vtpConf.hps.single_trig[ii].pde_en             ? (1<< 4) : 0;
	  enable_flags |= vtpConf.hps.single_trig[ii].hodo_l1_en         ? (1<< 5) : 0;
	  enable_flags |= vtpConf.hps.single_trig[ii].hodo_l2_en         ? (1<< 6) : 0;
	  enable_flags |= vtpConf.hps.single_trig[ii].hodo_l1l2_geom_en  ? (1<< 7) : 0;
	  enable_flags |= vtpConf.hps.single_trig[ii].hodo_l1l2x_geom_en ? (1<< 8) : 0;
	  enable_flags |= vtpConf.hps.single_trig[ii].en                 ? (1<<31) : 0;

	  vtpSetHPS_SingleTrigger(ii, 0,
				  vtpConf.hps.single_trig[ii].cluster_emin,
				  vtpConf.hps.single_trig[ii].cluster_emax,
				  vtpConf.hps.single_trig[ii].cluster_nmin,
				  vtpConf.hps.single_trig[ii].cluster_xmin,
				  vtpConf.hps.single_trig[ii].pde_c,
				  enable_flags
				  );

	  vtpSetHPS_SingleTrigger(ii, 1,
				  vtpConf.hps.single_trig[ii].cluster_emin,
				  vtpConf.hps.single_trig[ii].cluster_emax,
				  vtpConf.hps.single_trig[ii].cluster_nmin,
				  vtpConf.hps.single_trig[ii].cluster_xmin,
				  vtpConf.hps.single_trig[ii].pde_c,
				  enable_flags
				  );
	}

      for(ii=0;ii<4;ii++)
	{
	  enable_flags = 0;
	  enable_flags |= vtpConf.hps.pair_trig[ii].pair_esum_en         ? (1<< 0) : 0;
	  enable_flags |= vtpConf.hps.pair_trig[ii].pair_ediff_en        ? (1<< 1) : 0;
	  enable_flags |= vtpConf.hps.pair_trig[ii].pair_coplanarity_en  ? (1<< 2) : 0;
	  enable_flags |= vtpConf.hps.pair_trig[ii].pair_ed_en           ? (1<< 3) : 0;
	  enable_flags |= vtpConf.hps.pair_trig[ii].hodo_l1_en           ? (1<< 4) : 0;
	  enable_flags |= vtpConf.hps.pair_trig[ii].hodo_l2_en           ? (1<< 5) : 0;
	  enable_flags |= vtpConf.hps.pair_trig[ii].hodo_l1l2_geom_en    ? (1<< 6) : 0;
	  enable_flags |= vtpConf.hps.pair_trig[ii].hodo_l1l2x_geom_en   ? (1<< 7) : 0;
	  enable_flags |= vtpConf.hps.pair_trig[ii].en                   ? (1<<31) : 0;

	  vtpSetHPS_PairTrigger(ii,
				vtpConf.hps.pair_trig[ii].cluster_emin,
				vtpConf.hps.pair_trig[ii].cluster_emax,
				vtpConf.hps.pair_trig[ii].cluster_nmin,
				vtpConf.hps.pair_trig[ii].pair_dt,
				vtpConf.hps.pair_trig[ii].pair_esum_min,
				vtpConf.hps.pair_trig[ii].pair_esum_max,
				vtpConf.hps.pair_trig[ii].pair_ediff_max,
				vtpConf.hps.pair_trig[ii].pair_ed_factor,
				vtpConf.hps.pair_trig[ii].pair_ed_thr,
				vtpConf.hps.pair_trig[ii].pair_coplanarity_tol,
				enable_flags
				);
	}

      for(ii=0;ii<2;ii++)
	{
	  enable_flags = 0;
	  enable_flags |= vtpConf.hps.mult_trig[ii].en     ? (1<<31) : 0;
	  vtpSetHPS_MultiplicityTrigger(
					ii,
					vtpConf.hps.mult_trig[ii].cluster_emin,
					vtpConf.hps.mult_trig[ii].cluster_emax,
					vtpConf.hps.mult_trig[ii].cluster_nmin,
					vtpConf.hps.mult_trig[ii].mult_dt,
					vtpConf.hps.mult_trig[ii].mult_top_min,
					vtpConf.hps.mult_trig[ii].mult_bot_min,
					vtpConf.hps.mult_trig[ii].mult_tot_min,
					enable_flags
					);
	}

      enable_flags = 0;
      enable_flags |= vtpConf.hps.fee_trig.en     ? (1<<31) : 0;
      vtpSetHPS_FeeTrigger(
			   vtpConf.hps.fee_trig.cluster_emin,
			   vtpConf.hps.fee_trig.cluster_emax,
			   vtpConf.hps.fee_trig.cluster_nmin,
			   vtpConf.hps.fee_trig.prescale_xmin,
			   vtpConf.hps.fee_trig.prescale_xmax,
			   vtpConf.hps.fee_trig.prescale,
			   enable_flags
			   );

      enable_flags = 0;
      enable_flags |= vtpConf.hps.pair_trig[ii].pair_esum_en         ? (1<< 0) : 0;
      enable_flags |= vtpConf.hps.pair_trig[ii].pair_ediff_en        ? (1<< 1) : 0;
      enable_flags |= vtpConf.hps.pair_trig[ii].pair_coplanarity_en  ? (1<< 2) : 0;
      enable_flags |= vtpConf.hps.pair_trig[ii].pair_ed_en           ? (1<< 3) : 0;
      enable_flags |= vtpConf.hps.pair_trig[ii].hodo_l1_en           ? (1<< 4) : 0;
      enable_flags |= vtpConf.hps.pair_trig[ii].hodo_l2_en           ? (1<< 5) : 0;
      enable_flags |= vtpConf.hps.pair_trig[ii].hodo_l1l2_geom_en    ? (1<< 6) : 0;
      enable_flags |= vtpConf.hps.pair_trig[ii].hodo_l1l2x_geom_en   ? (1<< 7) : 0;
      enable_flags |= vtpConf.hps.pair_trig[ii].en                   ? (1<<31) : 0;

      vtpSetHPS_PairTrigger(ii,
          vtpConf.hps.pair_trig[ii].cluster_emin,
          vtpConf.hps.pair_trig[ii].cluster_emax,
          vtpConf.hps.pair_trig[ii].cluster_nmin,
          vtpConf.hps.pair_trig[ii].pair_dt,
          vtpConf.hps.pair_trig[ii].pair_esum_min,
          vtpConf.hps.pair_trig[ii].pair_esum_max,
          vtpConf.hps.pair_trig[ii].pair_ediff_max,
          vtpConf.hps.pair_trig[ii].pair_ed_factor,
          vtpConf.hps.pair_trig[ii].pair_ed_thr,
          vtpConf.hps.pair_trig[ii].pair_coplanarity_tol,
          enable_flags
        );
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_COMPTON)
  {
    for(ii=0;ii<5;ii++)
    {
      vtpSetTriggerBitPrescaler(ii, vtpConf.compton.trig.prescale[ii]);
      vtpSetTriggerBitDelay(ii, vtpConf.compton.trig.delay[ii]);
      vtpSetCompton_Trigger(ii, vtpConf.compton.fadc_threshold[ii], vtpConf.compton.eplane_mult_min[ii], vtpConf.compton.eplane_mask[ii], vtpConf.compton.fadc_mask[ii]);
    }

    vtpSetCompton_EnableScalerReadout(vtpConf.compton.enable_scaler_readout);
    vtpSetCompton_VetrocWidth(vtpConf.compton.vetroc_width);
    vtpSetGt_latency(vtpConf.compton.trig.latency);
    vtpSetGt_width(vtpConf.compton.trig.width);
  }

  if((vtpConf.fw_type[0] == VTP_FW_TYPE_FADCSTREAM))
  {
    for(inst=0;inst<1;inst++)
    {
      vtpStreamingSetEbCfg(
	  vtpConf.payload_en,		   
          vtpConf.fadc_streaming.eb[inst].nstreams,
          vtpConf.fadc_streaming.frame_len,
          vtpConf.fadc_streaming.roc_id,
	  NULL
        );

      printf("DEBUG destip (%d) = %d %d %d %d\n",inst, 
	     vtpConf.fadc_streaming.eb[inst].destip[0],
	     vtpConf.fadc_streaming.eb[inst].destip[1],
	     vtpConf.fadc_streaming.eb[inst].destip[2],
	     vtpConf.fadc_streaming.eb[inst].destip[3]);
      vtpStreamingSetTcpCfg(
          inst,
          vtpConf.fadc_streaming.eb[inst].ipaddr,
          vtpConf.fadc_streaming.eb[inst].subnet,
          vtpConf.fadc_streaming.eb[inst].gateway,
          vtpConf.fadc_streaming.eb[inst].mac,
          vtpConf.fadc_streaming.eb[inst].destip,
          vtpConf.fadc_streaming.eb[inst].destipport,
	  vtpConf.fadc_streaming.eb[inst].localport
        );

	  /* do it from readout list
      vtpStreamingTcpConnect(
          inst,
          vtpConf.fadc_streaming.eb[inst].connect
        );
	  */
    }
  }


  // VTP ROC Configuration
  if((vtpConf.fw_type[0] == VTP_FW_TYPE_VCODAROC)&&(vtpConf.fw_type[1] == ZYNC_FW_TYPE_ZCODAROC)) {
    printf("vtpDownloadAll: Writing VTP ROC Parameters \n");
    //vtpRocSetID(vtpConf.vtp_roc.roc_id);
    printf("vtpDownloadAll: ROC ID = %d  Dest IP = 0x%08x  Dest port = %d\n",
	   vtpConf.vtp_roc.roc_id,vtpConf.vtp_roc.destip,vtpConf.vtp_roc.destipport);
    printf("vtpDownloadAll: Payload port enable mask = 0x%04x\n",vtpConf.payload_en);
  }else{
    printf("vtpDownloadAll: Payload port enable mask = 0x%04x\n",vtpConf.payload_en);
  }

  vtpUploadAllPrint();

  return(0);
}

int
vtpUploadAll(char *string, int length)
{
  int i, len1, len2, enable_flags, inst;
  char *str, sss[4096];

  for(i=0;i<2;i++) {
    vtpConf.fw_rev[i] = vtpGetFW_Version(i);
    vtpConf.fw_type[i] = vtpGetFW_Type(i);
  }

  vtpConf.window_width = vtpGetWindowWidth();
  vtpConf.window_offset = vtpGetWindowLookback();

  //  inconsistant with new Streaming firmware - DJA
  //  this is returned from the vtpStreamingGetEbCfg() function instead
  //  vtpConf.payload_en = vtpGetTriggerPayloadMask();
  vtpConf.fiber_en = vtpGetTriggerFiberMask();

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_EC)
    {
      // EC configuration
      vtpGetFadcSum_MaskEn(vtpConf.ec.fadcsum_ch_en);

      vtpGetECtrig_emin(0, &vtpConf.ec.inner.hit_emin);
      vtpGetECtrig_dt(0, &vtpConf.ec.inner.hit_dt);
      vtpGetECcosmic_emin(0, &vtpConf.ec.inner.cosmic_emin);
      vtpGetECcosmic_multmax(0, &vtpConf.ec.inner.cosmic_multmax);
      vtpGetECcosmic_width(0, &vtpConf.ec.inner.cosmic_hitwidth);
      vtpGetECcosmic_delay(0, &vtpConf.ec.inner.cosmic_evaldelay);
      vtpGetECtrig_dalitz(0, &vtpConf.ec.inner.dalitz_min, &vtpConf.ec.inner.dalitz_max);
      vtpConf.ec.inner.dalitz_min = vtpConf.ec.inner.dalitz_min/8;
      vtpConf.ec.inner.dalitz_max = vtpConf.ec.inner.dalitz_max/8;

      vtpGetECtrig_emin(1, &vtpConf.ec.outer.hit_emin);
      vtpGetECtrig_dt(1, &vtpConf.ec.outer.hit_dt);
      vtpGetECcosmic_emin(1, &vtpConf.ec.outer.cosmic_emin);
      vtpGetECcosmic_multmax(1, &vtpConf.ec.outer.cosmic_multmax);
      vtpGetECcosmic_width(1, &vtpConf.ec.outer.cosmic_hitwidth);
      vtpGetECcosmic_delay(1, &vtpConf.ec.outer.cosmic_evaldelay);
      vtpGetECtrig_dalitz(1, &vtpConf.ec.outer.dalitz_min, &vtpConf.ec.outer.dalitz_max);
      vtpConf.ec.outer.dalitz_min = vtpConf.ec.outer.dalitz_min/8;
      vtpConf.ec.outer.dalitz_max = vtpConf.ec.outer.dalitz_max/8;
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_PC)
    {
      // PC configuration
      vtpGetFadcSum_MaskEn(vtpConf.pc.fadcsum_ch_en);

      vtpGetPCcosmic_emin(&vtpConf.pc.cosmic_emin);
      vtpGetPCcosmic_multmax(&vtpConf.pc.cosmic_multmax);
      vtpGetPCcosmic_width(&vtpConf.pc.cosmic_hitwidth);
      vtpGetPCcosmic_delay(&vtpConf.pc.cosmic_evaldelay);
      vtpGetPCcosmic_pixel(&vtpConf.pc.cosmic_pixelen);
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_HTCC)
    {
      // HTCC configuration
      vtpGetHTCC_thresholds(&vtpConf.htcc.threshold[0], &vtpConf.htcc.threshold[1], &vtpConf.htcc.threshold[2]);
      vtpGetHTCC_nframes(&vtpConf.htcc.nframes);

      vtpGetCTOF_thresholds(&vtpConf.htcc.ctof_threshold[0], &vtpConf.htcc.ctof_threshold[1], &vtpConf.htcc.ctof_threshold[2]);
      vtpGetCTOF_nframes(&vtpConf.htcc.ctof_nframes);
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_FTOF)
    {
      // FTOF configuration
      vtpGetFTOF_thresholds(&vtpConf.ftof.threshold[0], &vtpConf.ftof.threshold[1], &vtpConf.ftof.threshold[2]);
      vtpGetFTOF_nframes(&vtpConf.ftof.nframes);
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_CND)
    {
      // CND configuration
      vtpGetCND_thresholds(&vtpConf.cnd.threshold[0], &vtpConf.cnd.threshold[1], &vtpConf.cnd.threshold[2]);
      vtpGetCND_nframes(&vtpConf.cnd.nframes);
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_PCS)
    {
      // PCS configuration
      vtpGetPCS_thresholds(&vtpConf.pcs.threshold[0], &vtpConf.pcs.threshold[1], &vtpConf.pcs.threshold[2]);
      vtpGetPCS_nframes(&vtpConf.pcs.nframes);
      vtpGetPCS_dipfactor(&vtpConf.pcs.dipfactor);
      vtpGetPCS_nstrip(&vtpConf.pcs.nstrip_min, &vtpConf.pcs.nstrip_max);
      vtpGetPCS_dalitz(&vtpConf.pcs.dalitz_min, &vtpConf.pcs.dalitz_max);
      vtpGetPCU_thresholds(&vtpConf.pcs.pcu_threshold[0], &vtpConf.pcs.pcu_threshold[1], &vtpConf.pcs.pcu_threshold[2]);
      vtpGetPCcosmic_emin(&vtpConf.pcs.cosmic_emin);
      vtpGetPCcosmic_multmax(&vtpConf.pcs.cosmic_multmax);
      vtpGetPCcosmic_width(&vtpConf.pcs.cosmic_hitwidth);
      vtpGetPCcosmic_delay(&vtpConf.pcs.cosmic_evaldelay);
      vtpGetPCcosmic_pixel(&vtpConf.pcs.cosmic_pixelen);
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_ECS)
    {
      // ECS configuration
      vtpGetFadcSum_MaskEn(vtpConf.ecs.fadcsum_ch_en);

      vtpGetECcosmic_emin(0, &vtpConf.ecs.outer.cosmic_emin);
      vtpGetECcosmic_multmax(0, &vtpConf.ecs.outer.cosmic_multmax);
      vtpGetECcosmic_width(0, &vtpConf.ecs.outer.cosmic_hitwidth);
      vtpGetECcosmic_delay(0, &vtpConf.ecs.outer.cosmic_evaldelay);

      vtpGetECcosmic_emin(1, &vtpConf.ecs.outer.cosmic_emin);
      vtpGetECcosmic_multmax(1, &vtpConf.ecs.outer.cosmic_multmax);
      vtpGetECcosmic_width(1, &vtpConf.ecs.outer.cosmic_hitwidth);
      vtpGetECcosmic_delay(1, &vtpConf.ecs.outer.cosmic_evaldelay);

      vtpGetECS_thresholds(&vtpConf.ecs.threshold[0], &vtpConf.ecs.threshold[1], &vtpConf.ecs.threshold[2]);
      vtpGetECS_nframes(&vtpConf.ecs.nframes);
      vtpGetECS_dipfactor(&vtpConf.ecs.dipfactor);
      vtpGetECS_nstrip(&vtpConf.ecs.nstrip_min, &vtpConf.ecs.nstrip_max);
      vtpGetECS_dalitz(&vtpConf.ecs.dalitz_min, &vtpConf.ecs.dalitz_max);
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_GT)
    {
      // GT configuration
      vtpConf.gt.trig_latency = vtpGetGt_latency();
      vtpConf.gt.trig_width = vtpGetGt_width();

      for(i=0; i<32; i++)
	{
	  vtpGetGtTriggerBit(i,
			     &vtpConf.gt.trgbits[i].ssp_strigger_bit_mask[0],
			     &vtpConf.gt.trgbits[i].ssp_sector_mask[0],
			     &vtpConf.gt.trgbits[i].sector_mult_min[0],
			     &vtpConf.gt.trgbits[i].ssp_strigger_bit_mask[1],
			     &vtpConf.gt.trgbits[i].ssp_sector_mask[1],
			     &vtpConf.gt.trgbits[i].sector_mult_min[1],
			     &vtpConf.gt.trgbits[i].sector_coin_width,
			     &vtpConf.gt.trgbits[i].ssp_ctrigger_bit_mask,
			     &vtpConf.gt.trgbits[i].delay,
			     &vtpConf.gt.trgbits[i].pulser_freq,
			     &vtpConf.gt.trgbits[i].prescale
			     );
	}
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_DC)
    {
      vtpGetDc_SegmentThresholdMin(0, &vtpConf.dc.dcsegfind_threshold[0]);
      vtpGetDc_SegmentThresholdMin(1, &vtpConf.dc.dcsegfind_threshold[1]);
      vtpGetDc_RoadId(vtpConf.dc.roadid);
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_HCAL)
    {
      vtpGetHcal_ClusterCoincidence(&vtpConf.hcal.hit_dt);
      vtpGetHcal_ClusterThreshold(&vtpConf.hcal.cluster_emin);
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_FTCAL)
    {
      vtpGetHPS_PairTrigger(i,
          &vtpConf.hps.pair_trig[i].cluster_emin,
          &vtpConf.hps.pair_trig[i].cluster_emax,
          &vtpConf.hps.pair_trig[i].cluster_nmin,
          &vtpConf.hps.pair_trig[i].pair_dt,
          &vtpConf.hps.pair_trig[i].pair_esum_min,
          &vtpConf.hps.pair_trig[i].pair_esum_max,
          &vtpConf.hps.pair_trig[i].pair_ediff_max,
          &vtpConf.hps.pair_trig[i].pair_ed_factor,
          &vtpConf.hps.pair_trig[i].pair_ed_thr,
          &vtpConf.hps.pair_trig[i].pair_coplanarity_tol,
          &enable_flags
        );

      vtpConf.hps.pair_trig[i].pair_esum_en        = (enable_flags & 0x00000001) ? 1 : 0;
      vtpConf.hps.pair_trig[i].pair_ediff_en       = (enable_flags & 0x00000002) ? 1 : 0;
      vtpConf.hps.pair_trig[i].pair_coplanarity_en = (enable_flags & 0x00000004) ? 1 : 0;
      vtpConf.hps.pair_trig[i].pair_ed_en          = (enable_flags & 0x00000008) ? 1 : 0;
      vtpConf.hps.pair_trig[i].hodo_l1_en          = (enable_flags & 0x00000010) ? 1 : 0;
      vtpConf.hps.pair_trig[i].hodo_l2_en          = (enable_flags & 0x00000020) ? 1 : 0;
      vtpConf.hps.pair_trig[i].hodo_l1l2_geom_en   = (enable_flags & 0x00000040) ? 1 : 0;
      vtpConf.hps.pair_trig[i].hodo_l1l2x_geom_en  = (enable_flags & 0x00000080) ? 1 : 0;
      vtpConf.hps.pair_trig[i].en                  = (enable_flags & 0x80000000) ? 1 : 0;

    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_FTHODO)
    {
      vtpGetFTHODOemin(&vtpConf.fthodo.hit_emin);
    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_HPS)
    {
      vtpGetHPS_Cluster(&vtpConf.hps.cluster.top_nbottom, &vtpConf.hps.cluster.hit_dt, &vtpConf.hps.cluster.seed_thr);
      vtpGetHPS_Hodoscope(&vtpConf.hps.hodoscope.hit_dt, &vtpConf.hps.hodoscope.fadchit_thr, &vtpConf.hps.hodoscope.hodo_thr);

      for(i=0;i<4;i++)
	{
	  vtpGetHPS_SingleTrigger(i, 0,
				  &vtpConf.hps.single_trig[i].cluster_emin,
				  &vtpConf.hps.single_trig[i].cluster_emax,
				  &vtpConf.hps.single_trig[i].cluster_nmin,
				  &vtpConf.hps.single_trig[i].cluster_xmin,
				  &vtpConf.hps.single_trig[i].pde_c[0],
				  &enable_flags
				  );

	  /*
	    vtpGetHPS_SingleTrigger(i, 1,
	    &vtpConf.hps.single_trig[i].cluster_emin,
	    &vtpConf.hps.single_trig[i].cluster_emax,
	    &vtpConf.hps.single_trig[i].cluster_nmin,
	    &vtpConf.hps.single_trig[i].cluster_xmin,
	    &vtpConf.hps.single_trig[i].pde_c[0],
	    &enable_flags
	    );
	  */

	  vtpConf.hps.single_trig[i].cluster_emin_en    = (enable_flags & 0x00000001) ? 1 : 0;
	  vtpConf.hps.single_trig[i].cluster_emax_en    = (enable_flags & 0x00000002) ? 1 : 0;
	  vtpConf.hps.single_trig[i].cluster_nmin_en    = (enable_flags & 0x00000004) ? 1 : 0;
	  vtpConf.hps.single_trig[i].cluster_xmin_en    = (enable_flags & 0x00000008) ? 1 : 0;
	  vtpConf.hps.single_trig[i].pde_en             = (enable_flags & 0x00000010) ? 1 : 0;
	  vtpConf.hps.single_trig[i].hodo_l1_en         = (enable_flags & 0x00000020) ? 1 : 0;
	  vtpConf.hps.single_trig[i].hodo_l2_en         = (enable_flags & 0x00000040) ? 1 : 0;
	  vtpConf.hps.single_trig[i].hodo_l1l2_geom_en  = (enable_flags & 0x00000080) ? 1 : 0;
	  vtpConf.hps.single_trig[i].hodo_l1l2x_geom_en = (enable_flags & 0x00000100) ? 1 : 0;
	  vtpConf.hps.single_trig[i].en                 = (enable_flags & 0x80000000) ? 1 : 0;
	}

      for(i=0;i<4;i++)
	{
	  vtpGetHPS_PairTrigger(i,
				&vtpConf.hps.pair_trig[i].cluster_emin,
				&vtpConf.hps.pair_trig[i].cluster_emax,
				&vtpConf.hps.pair_trig[i].cluster_nmin,
				&vtpConf.hps.pair_trig[i].pair_dt,
				&vtpConf.hps.pair_trig[i].pair_esum_min,
				&vtpConf.hps.pair_trig[i].pair_esum_max,
				&vtpConf.hps.pair_trig[i].pair_ediff_max,
				&vtpConf.hps.pair_trig[i].pair_ed_factor,
				&vtpConf.hps.pair_trig[i].pair_ed_thr,
				&vtpConf.hps.pair_trig[i].pair_coplanarity_tol,
				&enable_flags
				);

	  vtpConf.hps.pair_trig[i].pair_esum_en        = (enable_flags & 0x00000001) ? 1 : 0;
	  vtpConf.hps.pair_trig[i].pair_ediff_en       = (enable_flags & 0x00000002) ? 1 : 0;
	  vtpConf.hps.pair_trig[i].pair_coplanarity_en = (enable_flags & 0x00000004) ? 1 : 0;
	  vtpConf.hps.pair_trig[i].pair_ed_en          = (enable_flags & 0x00000008) ? 1 : 0;
	  vtpConf.hps.pair_trig[i].hodo_l1_en          = (enable_flags & 0x00000010) ? 1 : 0;
	  vtpConf.hps.pair_trig[i].hodo_l2_en          = (enable_flags & 0x00000020) ? 1 : 0;
	  vtpConf.hps.pair_trig[i].hodo_l1l2_geom_en   = (enable_flags & 0x00000040) ? 1 : 0;
	  vtpConf.hps.pair_trig[i].hodo_l1l2x_geom_en  = (enable_flags & 0x00000080) ? 1 : 0;
	  vtpConf.hps.pair_trig[i].en                  = (enable_flags & 0x80000000) ? 1 : 0;

	}

      for(i=0;i<2;i++)
	{
	  vtpGetHPS_MultiplicityTrigger(
					i,
					&vtpConf.hps.mult_trig[i].cluster_emin,
					&vtpConf.hps.mult_trig[i].cluster_emax,
					&vtpConf.hps.mult_trig[i].cluster_nmin,
					&vtpConf.hps.mult_trig[i].mult_dt,
					&vtpConf.hps.mult_trig[i].mult_top_min,
					&vtpConf.hps.mult_trig[i].mult_bot_min,
					&vtpConf.hps.mult_trig[i].mult_tot_min,
					&enable_flags
					);
	  vtpConf.hps.mult_trig[i].en     = (enable_flags & 0x80000000) ? 1 : 0;
	}


      vtpGetHPS_FeeTrigger(
			   &vtpConf.hps.fee_trig.cluster_emin,
			   &vtpConf.hps.fee_trig.cluster_emax,
			   &vtpConf.hps.fee_trig.cluster_nmin,
			   vtpConf.hps.fee_trig.prescale_xmin,
			   vtpConf.hps.fee_trig.prescale_xmax,
			   vtpConf.hps.fee_trig.prescale,
			   &enable_flags
			   );
      vtpConf.hps.fee_trig.en             = (enable_flags & 0x80000000) ? 1 : 0;

      if((vtpConf.fw_type[0] == VTP_FW_TYPE_FADCSTREAM))
  {
    for(inst=0;inst<1;inst++)
    {
      vtpStreamingGetEbCfg(
          &vtpConf.payload_en,
          &vtpConf.fadc_streaming.eb[inst].nstreams,
          &vtpConf.fadc_streaming.frame_len,
          &vtpConf.fadc_streaming.roc_id
        );

      vtpStreamingGetTcpCfg(
          inst,
          vtpConf.fadc_streaming.eb[inst].ipaddr,
          vtpConf.fadc_streaming.eb[inst].subnet,
          vtpConf.fadc_streaming.eb[inst].gateway,
          vtpConf.fadc_streaming.eb[inst].mac,
          vtpConf.fadc_streaming.eb[inst].destip,
          &vtpConf.fadc_streaming.eb[inst].destipport,
	  &vtpConf.fadc_streaming.eb[inst].localport
        );
    }
  }


    }

  if(vtpConf.fw_type[0] == VTP_FW_TYPE_COMPTON)
  {
    for(i=0;i<5;i++)
    {
      vtpConf.compton.trig.prescale[i] = vtpGetTriggerBitPrescaler(i);
      vtpGetTriggerBitDelay(i, &vtpConf.compton.trig.delay[i]);
      vtpGetCompton_Trigger(i, &vtpConf.compton.fadc_threshold[i], &vtpConf.compton.eplane_mult_min[i], &vtpConf.compton.eplane_mask[i], &vtpConf.compton.fadc_mask[i]);
    }
    vtpGetCompton_EnableScalerReadout(&vtpConf.compton.enable_scaler_readout);
    vtpGetCompton_VetrocWidth(&vtpConf.compton.vetroc_width);
    vtpGetGt_latency(&vtpConf.compton.trig.latency);
    vtpGetGt_width(&vtpConf.compton.trig.width);

  }

  if(length)
    {
      str = string;
      str[0] = '\0';

      sprintf(sss, "VTP_FIRMWAREVERSION %d\n", vtpConf.fw_rev[0]); ADD_TO_STRING;
      sprintf(sss, "VTP_FIRMWARETYPE %d\n", vtpConf.fw_type[0]); ADD_TO_STRING;
      sprintf(sss, "VTP_W_WIDTH %d\n",vtpConf.window_width); ADD_TO_STRING;
      sprintf(sss, "VTP_W_OFFSET %d\n",vtpConf.window_offset); ADD_TO_STRING;
      sprintf(sss, "VTP_PAYLOAD_EN %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
	      (vtpConf.payload_en>>0)&0x1, (vtpConf.payload_en>>1)&0x1, (vtpConf.payload_en>>2)&0x1, (vtpConf.payload_en>>3)&0x1,
	      (vtpConf.payload_en>>4)&0x1, (vtpConf.payload_en>>5)&0x1, (vtpConf.payload_en>>6)&0x1, (vtpConf.payload_en>>7)&0x1,
	      (vtpConf.payload_en>>8)&0x1, (vtpConf.payload_en>>9)&0x1, (vtpConf.payload_en>>10)&0x1, (vtpConf.payload_en>>11)&0x1,
	      (vtpConf.payload_en>>12)&0x1, (vtpConf.payload_en>>13)&0x1, (vtpConf.payload_en>>14)&0x1, (vtpConf.payload_en>>15)&0x1
	      ); ADD_TO_STRING;
      sprintf(sss, "VTP_FIBER_EN %d %d %d %d\n",
	      (vtpConf.fiber_en>>0)&0x1, (vtpConf.fiber_en>>1)&0x1, (vtpConf.fiber_en>>2)&0x1, (vtpConf.fiber_en>>3)&0x1); ADD_TO_STRING;



      if(vtpConf.fw_type[0] == VTP_FW_TYPE_EC)
	{
	  sprintf(sss, "VTP_EC_FADCSUM_CH 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X\n",
		  vtpConf.ec.fadcsum_ch_en[0], vtpConf.ec.fadcsum_ch_en[1],
		  vtpConf.ec.fadcsum_ch_en[2], vtpConf.ec.fadcsum_ch_en[3],
		  vtpConf.ec.fadcsum_ch_en[4], vtpConf.ec.fadcsum_ch_en[5],
		  vtpConf.ec.fadcsum_ch_en[6], vtpConf.ec.fadcsum_ch_en[7],
		  vtpConf.ec.fadcsum_ch_en[8], vtpConf.ec.fadcsum_ch_en[9],
		  vtpConf.ec.fadcsum_ch_en[10], vtpConf.ec.fadcsum_ch_en[11],
		  vtpConf.ec.fadcsum_ch_en[12], vtpConf.ec.fadcsum_ch_en[13],
		  vtpConf.ec.fadcsum_ch_en[14], vtpConf.ec.fadcsum_ch_en[15]
		  ); ADD_TO_STRING;

	  sprintf(sss, "VTP_EC_INNER_HIT_EMIN %d\n", vtpConf.ec.inner.hit_emin); ADD_TO_STRING;
	  sprintf(sss, "VTP_EC_INNER_HIT_DT %d\n", vtpConf.ec.inner.hit_dt); ADD_TO_STRING;
	  sprintf(sss, "VTP_EC_INNER_COSMIC_EMIN %d\n", vtpConf.ec.inner.cosmic_emin); ADD_TO_STRING;
	  sprintf(sss, "VTP_EC_INNER_COSMIC_MULTMAX %d\n", vtpConf.ec.inner.cosmic_multmax); ADD_TO_STRING;
	  sprintf(sss, "VTP_EC_INNER_COSMIC_HITWIDTH %d\n", vtpConf.ec.inner.cosmic_hitwidth); ADD_TO_STRING;
	  sprintf(sss, "VTP_EC_INNER_COSMIC_EVALDELAY %d\n", vtpConf.ec.inner.cosmic_evaldelay); ADD_TO_STRING;
	  sprintf(sss, "VTP_EC_INNER_HIT_DALITZ %d %d\n", vtpConf.ec.inner.dalitz_min, vtpConf.ec.inner.dalitz_max); ADD_TO_STRING;

	  sprintf(sss, "VTP_EC_OUTER_HIT_EMIN %d\n", vtpConf.ec.outer.hit_emin); ADD_TO_STRING;
	  sprintf(sss, "VTP_EC_OUTER_HIT_DT %d\n", vtpConf.ec.outer.hit_dt); ADD_TO_STRING;
	  sprintf(sss, "VTP_EC_OUTER_COSMIC_EMIN %d\n", vtpConf.ec.outer.cosmic_emin); ADD_TO_STRING;
	  sprintf(sss, "VTP_EC_OUTER_COSMIC_MULTMAX %d\n", vtpConf.ec.outer.cosmic_multmax); ADD_TO_STRING;
	  sprintf(sss, "VTP_EC_OUTER_COSMIC_HITWIDTH %d\n", vtpConf.ec.outer.cosmic_hitwidth); ADD_TO_STRING;
	  sprintf(sss, "VTP_EC_OUTER_COSMIC_EVALDELAY %d\n", vtpConf.ec.outer.cosmic_evaldelay); ADD_TO_STRING;
	  sprintf(sss, "VTP_EC_OUTER_HIT_DALITZ %d %d\n", vtpConf.ec.outer.dalitz_min, vtpConf.ec.outer.dalitz_max); ADD_TO_STRING;
	}

      if(vtpConf.fw_type[0] == VTP_FW_TYPE_PC)
	{
	  sprintf(sss, "VTP_PC_FADCSUM_CH 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X\n",
		  vtpConf.pc.fadcsum_ch_en[0], vtpConf.pc.fadcsum_ch_en[1],
		  vtpConf.pc.fadcsum_ch_en[2], vtpConf.pc.fadcsum_ch_en[3],
		  vtpConf.pc.fadcsum_ch_en[4], vtpConf.pc.fadcsum_ch_en[5],
		  vtpConf.pc.fadcsum_ch_en[6], vtpConf.pc.fadcsum_ch_en[7],
		  vtpConf.pc.fadcsum_ch_en[8], vtpConf.pc.fadcsum_ch_en[9],
		  vtpConf.pc.fadcsum_ch_en[10], vtpConf.pc.fadcsum_ch_en[11],
		  vtpConf.pc.fadcsum_ch_en[12], vtpConf.pc.fadcsum_ch_en[13],
		  vtpConf.pc.fadcsum_ch_en[14], vtpConf.pc.fadcsum_ch_en[15]
		  ); ADD_TO_STRING;

	  sprintf(sss, "VTP_PC_COSMIC_EMIN %d\n", vtpConf.pc.cosmic_emin); ADD_TO_STRING;
	  sprintf(sss, "VTP_PC_COSMIC_MULTMAX %d\n", vtpConf.pc.cosmic_multmax); ADD_TO_STRING;
	  sprintf(sss, "VTP_PC_COSMIC_HITWIDTH %d\n", vtpConf.pc.cosmic_hitwidth); ADD_TO_STRING;
	  sprintf(sss, "VTP_PC_COSMIC_EVALDELAY %d\n", vtpConf.pc.cosmic_evaldelay); ADD_TO_STRING;
	  sprintf(sss, "VTP_PC_COSMIC_PIXELEN %d\n", vtpConf.pc.cosmic_pixelen); ADD_TO_STRING;
	}

      if(vtpConf.fw_type[0] == VTP_FW_TYPE_HTCC)
	{
	  sprintf(sss, "VTP_HTCC_THRESHOLDS %d %d %d\n", vtpConf.htcc.threshold[0], vtpConf.htcc.threshold[1], vtpConf.htcc.threshold[2]); ADD_TO_STRING;
	  sprintf(sss, "VTP_HTCC_NFRAMES %d\n", vtpConf.htcc.nframes); ADD_TO_STRING;

	  sprintf(sss, "VTP_CTOF_THRESHOLDS %d %d %d\n", vtpConf.htcc.ctof_threshold[0], vtpConf.htcc.ctof_threshold[1], vtpConf.htcc.ctof_threshold[2]); ADD_TO_STRING;
	  sprintf(sss, "VTP_CTOF_NFRAMES %d\n", vtpConf.htcc.ctof_nframes); ADD_TO_STRING;
	}

      if(vtpConf.fw_type[0] == VTP_FW_TYPE_FTOF)
	{
	  sprintf(sss, "VTP_FTOF_THRESHOLDS %d %d %d\n", vtpConf.ftof.threshold[0], vtpConf.ftof.threshold[1], vtpConf.ftof.threshold[2]); ADD_TO_STRING;
	  sprintf(sss, "VTP_FTOF_NFRAMES %d\n", vtpConf.ftof.nframes); ADD_TO_STRING;
	}

      if(vtpConf.fw_type[0] == VTP_FW_TYPE_CND)
	{
	  sprintf(sss, "VTP_CND_THRESHOLDS %d %d %d\n", vtpConf.cnd.threshold[0], vtpConf.cnd.threshold[1], vtpConf.cnd.threshold[2]); ADD_TO_STRING;
	  sprintf(sss, "VTP_CND_NFRAMES %d\n", vtpConf.cnd.nframes); ADD_TO_STRING;
	}

      if(vtpConf.fw_type[0] == VTP_FW_TYPE_PCS)
	{
	  sprintf(sss, "VTP_PCS_THRESHOLDS %d %d %d\n", vtpConf.pcs.threshold[0], vtpConf.pcs.threshold[1], vtpConf.pcs.threshold[2]); ADD_TO_STRING;
	  sprintf(sss, "VTP_PCS_NFRAMES %d\n", vtpConf.pcs.nframes); ADD_TO_STRING;
	  sprintf(sss, "VTP_PCS_DIPFACTOR %d\n", vtpConf.pcs.dipfactor); ADD_TO_STRING;
	  sprintf(sss, "VTP_PCS_NSTRIP %d %d\n", vtpConf.pcs.nstrip_min, vtpConf.pcs.nstrip_max); ADD_TO_STRING;
	  sprintf(sss, "VTP_PCS_DALITZ %d %d\n", vtpConf.pcs.dalitz_min, vtpConf.pcs.dalitz_max); ADD_TO_STRING;
	  sprintf(sss, "VTP_PCU_THRESHOLDS %d %d %d\n", vtpConf.pcs.pcu_threshold[0], vtpConf.pcs.pcu_threshold[1], vtpConf.pcs.pcu_threshold[2]); ADD_TO_STRING;
	  sprintf(sss, "VTP_PCS_COSMIC_EMIN %d\n", vtpConf.pcs.cosmic_emin); ADD_TO_STRING;
	  sprintf(sss, "VTP_PCS_COSMIC_MULTMAX %d\n", vtpConf.pcs.cosmic_multmax); ADD_TO_STRING;
	  sprintf(sss, "VTP_PCS_COSMIC_HITWIDTH %d\n", vtpConf.pcs.cosmic_hitwidth); ADD_TO_STRING;
	  sprintf(sss, "VTP_PCS_COSMIC_EVALDELAY %d\n", vtpConf.pcs.cosmic_evaldelay); ADD_TO_STRING;
	  sprintf(sss, "VTP_PCS_COSMIC_PIXELEN %d\n", vtpConf.pcs.cosmic_pixelen); ADD_TO_STRING;
	}

      if(vtpConf.fw_type[0] == VTP_FW_TYPE_ECS)
	{
	  sprintf(sss, "VTP_ECS_FADCSUM_CH 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X\n",
		  vtpConf.ecs.fadcsum_ch_en[0], vtpConf.ecs.fadcsum_ch_en[1],
		  vtpConf.ecs.fadcsum_ch_en[2], vtpConf.ecs.fadcsum_ch_en[3],
		  vtpConf.ecs.fadcsum_ch_en[4], vtpConf.ecs.fadcsum_ch_en[5],
		  vtpConf.ecs.fadcsum_ch_en[6], vtpConf.ecs.fadcsum_ch_en[7],
		  vtpConf.ecs.fadcsum_ch_en[8], vtpConf.ecs.fadcsum_ch_en[9],
		  vtpConf.ecs.fadcsum_ch_en[10], vtpConf.ecs.fadcsum_ch_en[11],
		  vtpConf.ecs.fadcsum_ch_en[12], vtpConf.ecs.fadcsum_ch_en[13],
		  vtpConf.ecs.fadcsum_ch_en[14], vtpConf.ecs.fadcsum_ch_en[15]
		  ); ADD_TO_STRING;

	  sprintf(sss, "VTP_ECS_THRESHOLDS %d %d %d\n", vtpConf.ecs.threshold[0], vtpConf.ecs.threshold[1], vtpConf.ecs.threshold[2]); ADD_TO_STRING;
	  sprintf(sss, "VTP_ECS_NFRAMES %d\n", vtpConf.ecs.nframes); ADD_TO_STRING;
	  sprintf(sss, "VTP_ECS_DIPFACTOR %d\n", vtpConf.ecs.dipfactor); ADD_TO_STRING;
	  sprintf(sss, "VTP_ECS_NSTRIP %d %d\n", vtpConf.ecs.nstrip_min, vtpConf.ecs.nstrip_max); ADD_TO_STRING;
	  sprintf(sss, "VTP_ECS_DALITZ %d %d\n", vtpConf.ecs.dalitz_min, vtpConf.ecs.dalitz_max); ADD_TO_STRING;

	  sprintf(sss, "VTP_ECS_OUTER_COSMIC_EMIN %d\n", vtpConf.ecs.outer.cosmic_emin); ADD_TO_STRING;
	  sprintf(sss, "VTP_ECS_OUTER_COSMIC_MULTMAX %d\n", vtpConf.ecs.outer.cosmic_multmax); ADD_TO_STRING;
	  sprintf(sss, "VTP_ECS_OUTER_COSMIC_HITWIDTH %d\n", vtpConf.ecs.outer.cosmic_hitwidth); ADD_TO_STRING;
	  sprintf(sss, "VTP_ECS_OUTER_COSMIC_EVALDELAY %d\n", vtpConf.ecs.outer.cosmic_evaldelay); ADD_TO_STRING;

	  sprintf(sss, "VTP_ECS_INNER_COSMIC_EMIN %d\n", vtpConf.ecs.inner.cosmic_emin); ADD_TO_STRING;
	  sprintf(sss, "VTP_ECS_INNER_COSMIC_MULTMAX %d\n", vtpConf.ecs.inner.cosmic_multmax); ADD_TO_STRING;
	  sprintf(sss, "VTP_ECS_INNER_COSMIC_HITWIDTH %d\n", vtpConf.ecs.inner.cosmic_hitwidth); ADD_TO_STRING;
	  sprintf(sss, "VTP_ECS_INNER_COSMIC_EVALDELAY %d\n", vtpConf.ecs.inner.cosmic_evaldelay); ADD_TO_STRING;
	}

      if(vtpConf.fw_type[0] == VTP_FW_TYPE_GT)
	{
	  sprintf(sss, "VTP_GT_LATENCY %d\n", vtpConf.gt.trig_latency); ADD_TO_STRING;
	  sprintf(sss, "VTP_GT_WIDTH %d\n", vtpConf.gt.trig_width); ADD_TO_STRING;

	  for(i=0; i<32; i++)
	    {
	      sprintf(sss, "VTP_GT_TRG %d\n", i); ADD_TO_STRING;
	      sprintf(sss, "VTP_GT_TRG_SSP_STRIGGER_MASK 0x%08X 0x%08X\n",vtpConf.gt.trgbits[i].ssp_strigger_bit_mask[0],vtpConf.gt.trgbits[i].ssp_strigger_bit_mask[1]); ADD_TO_STRING;
	      sprintf(sss, "VTP_GT_TRG_SSP_CTRIGGER_MASK 0x%08X\n",vtpConf.gt.trgbits[i].ssp_ctrigger_bit_mask); ADD_TO_STRING;
	      sprintf(sss, "VTP_GT_TRG_SSP_SECTOR_MASK 0x%08X 0x%08X\n",vtpConf.gt.trgbits[i].ssp_sector_mask[0],vtpConf.gt.trgbits[i].ssp_sector_mask[1]); ADD_TO_STRING;
	      sprintf(sss, "VTP_GT_TRG_SSP_SECTOR_MULT_MIN %d %d\n",vtpConf.gt.trgbits[i].sector_mult_min[0],vtpConf.gt.trgbits[i].sector_mult_min[1]); ADD_TO_STRING;
	      sprintf(sss, "VTP_GT_TRG_SSP_SECTOR_WIDTH %d\n",vtpConf.gt.trgbits[i].sector_coin_width); ADD_TO_STRING;
	      sprintf(sss, "VTP_GT_TRG_PULSER_FREQ %.3f\n",vtpConf.gt.trgbits[i].pulser_freq); ADD_TO_STRING;
	      sprintf(sss, "VTP_GT_TRG_DELAY %d\n",vtpConf.gt.trgbits[i].delay); ADD_TO_STRING;
	      sprintf(sss, "VTP_GT_TRG_PRESCALE %d\n",vtpConf.gt.trgbits[i].prescale); ADD_TO_STRING;
	    }
	}

      if(vtpConf.fw_type[0] == VTP_FW_TYPE_DC)
	{
	  sprintf(sss, "VTP_DC_SEGTHR 0 %d\n", vtpConf.dc.dcsegfind_threshold[0]); ADD_TO_STRING;
	  sprintf(sss, "VTP_DC_SEGTHR 1 %d\n", vtpConf.dc.dcsegfind_threshold[1]); ADD_TO_STRING;
	  sprintf(sss, "VTP_DC_ROADID %s\n", vtpConf.dc.roadid); ADD_TO_STRING;
	}

      if(vtpConf.fw_type[0] == VTP_FW_TYPE_HCAL)
	{
	  sprintf(sss, "VTP_HCAL_HIT_DT %d\n", vtpConf.hcal.hit_dt); ADD_TO_STRING;
	  sprintf(sss, "VTP_HCAL_HIT_EMIN %d\n", vtpConf.hcal.cluster_emin); ADD_TO_STRING;
	}

      if(vtpConf.fw_type[0] == VTP_FW_TYPE_FTCAL)
	{
	  sprintf(sss, "VTP_FTCAL_FADCSUM_CH 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X 0x%04X\n",
		  vtpConf.ftcal.fadcsum_ch_en[0], vtpConf.ftcal.fadcsum_ch_en[1],
		  vtpConf.ftcal.fadcsum_ch_en[2], vtpConf.ftcal.fadcsum_ch_en[3],
		  vtpConf.ftcal.fadcsum_ch_en[4], vtpConf.ftcal.fadcsum_ch_en[5],
		  vtpConf.ftcal.fadcsum_ch_en[6], vtpConf.ftcal.fadcsum_ch_en[7],
		  vtpConf.ftcal.fadcsum_ch_en[8], vtpConf.ftcal.fadcsum_ch_en[9],
		  vtpConf.ftcal.fadcsum_ch_en[10], vtpConf.ftcal.fadcsum_ch_en[11],
		  vtpConf.ftcal.fadcsum_ch_en[12], vtpConf.ftcal.fadcsum_ch_en[13],
		  vtpConf.ftcal.fadcsum_ch_en[14], vtpConf.ftcal.fadcsum_ch_en[15]
		  ); ADD_TO_STRING;

	  sprintf(sss, "VTP_FTCAL_SEED_EMIN %d\n", vtpConf.ftcal.seed_emin); ADD_TO_STRING;
	  sprintf(sss, "VTP_FTCAL_SEED_DT %d\n", vtpConf.ftcal.seed_dt); ADD_TO_STRING;
	  sprintf(sss, "VTP_FTCAL_HODO_DT %d\n", vtpConf.ftcal.hodo_dt); ADD_TO_STRING;
	  sprintf(sss, "VTP_FTCAL_CLUSTER_DEADTIME_EMIN %d\n", vtpConf.ftcal.deadtime_emin); ADD_TO_STRING;
	  sprintf(sss, "VTP_FTCAL_CLUSTER_DEADTIME %d\n", vtpConf.ftcal.deadtime); ADD_TO_STRING;
	}

      /* HPS parameters */
      if(vtpConf.fw_type[0] == VTP_FW_TYPE_HPS)
	{
	  for(i=0;i<4;i++)
	    {
	      sprintf(sss, "VTP_HPS_PAIR_EMIN %d %d\n", i, vtpConf.hps.pair_trig[i].cluster_emin); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_PAIR_EMAX %d %d\n", i, vtpConf.hps.pair_trig[i].cluster_emax); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_PAIR_NMIN %d %d\n", i, vtpConf.hps.pair_trig[i].cluster_nmin); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_PAIR_TIMECOINCIDENCE %d %d\n", i, vtpConf.hps.pair_trig[i].pair_dt); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_PAIR_SUMMAX_MIN %d %d %d %d\n", i, vtpConf.hps.pair_trig[i].pair_esum_max, vtpConf.hps.pair_trig[i].pair_esum_min, vtpConf.hps.pair_trig[i].pair_esum_en); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_PAIR_DIFFMAX %d %d %d\n", i, vtpConf.hps.pair_trig[i].pair_ediff_max, vtpConf.hps.pair_trig[i].pair_ediff_en); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_PAIR_ENERGYDIST %d %f %d %d\n", i, vtpConf.hps.pair_trig[i].pair_ed_factor, vtpConf.hps.pair_trig[i].pair_ed_thr, vtpConf.hps.pair_trig[i].pair_ed_en); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_PAIR_COPLANARITY %d %d %d\n", i, vtpConf.hps.pair_trig[i].pair_coplanarity_tol, vtpConf.hps.pair_trig[i].pair_coplanarity_en); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_PAIR_EN %d %d\n", i, vtpConf.hps.pair_trig[i].en); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_PAIR_HODO %d %d %d %d %d\n", i, vtpConf.hps.pair_trig[i].hodo_l1_en, vtpConf.hps.pair_trig[i].hodo_l2_en, vtpConf.hps.pair_trig[i].hodo_l1l2_geom_en, vtpConf.hps.pair_trig[i].hodo_l1l2x_geom_en); ADD_TO_STRING;
	    }


	  sprintf(sss, "VTP_FTHODO_EMIN %d\n", vtpConf.fthodo.hit_emin); ADD_TO_STRING;

	  if(vtpConf.hps.cluster.top_nbottom)
	    {
	      sprintf(sss, "VTP_HPS_ECAL_TOP\n"); ADD_TO_STRING;
	    }
	  else
	    {
	      sprintf(sss, "VTP_HPS_ECAL_BOT\n"); ADD_TO_STRING;
	    }

	  sprintf(sss, "VTP_HPS_ECAL_CLUSTER_HIT_DT %d\n", vtpConf.hps.cluster.hit_dt); ADD_TO_STRING;

	  sprintf(sss, "VTP_HPS_ECAL_CLUSTER_SEED_THR %d\n",  vtpConf.hps.cluster.seed_thr); ADD_TO_STRING;
	  sprintf(sss, "VTP_HPS_HODOSCOPE_FADCHIT_THR %d\n",  vtpConf.hps.hodoscope.fadchit_thr); ADD_TO_STRING;
	  sprintf(sss, "VTP_HPS_HODOSCOPE_HODO_THR %d\n",     vtpConf.hps.hodoscope.hodo_thr); ADD_TO_STRING;
	  sprintf(sss, "VTP_HPS_HODOSCOPE_HODO_DT %d\n",      vtpConf.hps.hodoscope.hit_dt); ADD_TO_STRING;
	  sprintf(sss, "VTP_HPS_CALIB_HODOSCOPE_TOP_EN %d\n", vtpConf.hps.calib.hodoscope_top_en); ADD_TO_STRING;
	  sprintf(sss, "VTP_HPS_CALIB_HODOSCOPE_BOT_EN %d\n", vtpConf.hps.calib.hodoscope_bot_en); ADD_TO_STRING;
	  sprintf(sss, "VTP_HPS_CALIB_COSMIC_DT %d\n",        vtpConf.hps.calib.cosmic_dt); ADD_TO_STRING;
	  sprintf(sss, "VTP_HPS_CALIB_COSMIC_TOP_EN %d\n",    vtpConf.hps.calib.cosmic_top_en); ADD_TO_STRING;
	  sprintf(sss, "VTP_HPS_CALIB_COSMIC_BOT_EN %d\n",    vtpConf.hps.calib.cosmic_bot_en); ADD_TO_STRING;
	  sprintf(sss, "VTP_HPS_CALIB_PULSER_EN %d\n",        vtpConf.hps.calib.pulser_en); ADD_TO_STRING;
	  sprintf(sss, "VTP_HPS_CALIB_PULSER_FREQ %f\n",      vtpConf.hps.calib.pulser_freq); ADD_TO_STRING;

	  for(i=0;i<4;i++)
	    {
	      sprintf(sss, "VTP_HPS_SINGLE_EMIN %d %d %d\n", i, vtpConf.hps.single_trig[i].cluster_emin, vtpConf.hps.single_trig[i].cluster_emin_en); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_SINGLE_EMAX %d %d %d\n", i, vtpConf.hps.single_trig[i].cluster_emax, vtpConf.hps.single_trig[i].cluster_emax_en); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_SINGLE_NMIN %d %d %d\n", i, vtpConf.hps.single_trig[i].cluster_nmin, vtpConf.hps.single_trig[i].cluster_nmin_en); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_SINGLE_XMIN %d %d %d\n", i, vtpConf.hps.single_trig[i].cluster_xmin, vtpConf.hps.single_trig[i].cluster_xmin_en); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_SINGLE_PDE %d %f %f %f %f %d\n", i, vtpConf.hps.single_trig[i].pde_c[0], vtpConf.hps.single_trig[i].pde_c[1], vtpConf.hps.single_trig[i].pde_c[2], vtpConf.hps.single_trig[i].pde_c[3], vtpConf.hps.single_trig[i].pde_en); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_SINGLE_HODO %d %d %d %d %d\n", i, vtpConf.hps.single_trig[i].hodo_l1_en, vtpConf.hps.single_trig[i].hodo_l2_en, vtpConf.hps.single_trig[i].hodo_l1l2_geom_en, vtpConf.hps.single_trig[i].hodo_l1l2x_geom_en); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_SINGLE_EN %d %d\n", i, vtpConf.hps.single_trig[i].en); ADD_TO_STRING;
	    }

	  for(i=0;i<4;i++)
	    {
	      sprintf(sss, "VTP_HPS_PAIR_EMIN %d %d\n", i, vtpConf.hps.pair_trig[i].cluster_emin); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_PAIR_EMAX %d %d\n", i, vtpConf.hps.pair_trig[i].cluster_emax); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_PAIR_NMIN %d %d\n", i, vtpConf.hps.pair_trig[i].cluster_nmin); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_PAIR_TIMECOINCIDENCE %d %d\n", i, vtpConf.hps.pair_trig[i].pair_dt); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_PAIR_SUMMAX_MIN %d %d %d %d\n", i, vtpConf.hps.pair_trig[i].pair_esum_max, vtpConf.hps.pair_trig[i].pair_esum_min, vtpConf.hps.pair_trig[i].pair_esum_en); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_PAIR_DIFFMAX %d %d %d\n", i, vtpConf.hps.pair_trig[i].pair_ediff_max, vtpConf.hps.pair_trig[i].pair_ediff_en); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_PAIR_ENERGYDIST %d %f %d %d\n", i, vtpConf.hps.pair_trig[i].pair_ed_factor, vtpConf.hps.pair_trig[i].pair_ed_thr, vtpConf.hps.pair_trig[i].pair_ed_en); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_PAIR_COPLANARITY %d %d %d\n", i, vtpConf.hps.pair_trig[i].pair_coplanarity_tol, vtpConf.hps.pair_trig[i].pair_coplanarity_en); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_PAIR_EN %d %d\n", i, vtpConf.hps.pair_trig[i].en); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_PAIR_HODO %d %d %d %d %d\n", i, vtpConf.hps.pair_trig[i].hodo_l1_en, vtpConf.hps.pair_trig[i].hodo_l2_en, vtpConf.hps.pair_trig[i].hodo_l1l2_geom_en, vtpConf.hps.pair_trig[i].hodo_l1l2x_geom_en); ADD_TO_STRING;
	    }

	  for(i=0;i<2;i++)
	    {
	      sprintf(sss, "VTP_HPS_MULT_EMIN %d %d\n", i, vtpConf.hps.mult_trig[i].cluster_emin); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_MULT_EMAX %d %d\n", i, vtpConf.hps.mult_trig[i].cluster_emax); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_MULT_NMIN %d %d\n", i, vtpConf.hps.mult_trig[i].cluster_nmin); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_MULT_MIN %d %d %d %d\n", i, vtpConf.hps.mult_trig[i].mult_top_min, vtpConf.hps.mult_trig[i].mult_bot_min, vtpConf.hps.mult_trig[i].mult_tot_min); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_MULT_DT %d %d\n", i, vtpConf.hps.mult_trig[i].mult_dt); ADD_TO_STRING;
	      sprintf(sss, "VTP_HPS_MULT_EN %d %d\n", i, vtpConf.hps.mult_trig[i].en); ADD_TO_STRING;
	    }

	  sprintf(sss, "VTP_HPS_FEE_EN %d\n", vtpConf.hps.fee_trig.en); ADD_TO_STRING;
	  sprintf(sss, "VTP_HPS_FEE_EMIN %d\n", vtpConf.hps.fee_trig.cluster_emin); ADD_TO_STRING;
	  sprintf(sss, "VTP_HPS_FEE_EMAX %d\n", vtpConf.hps.fee_trig.cluster_emax); ADD_TO_STRING;
	  sprintf(sss, "VTP_HPS_FEE_NMIN %d\n", vtpConf.hps.fee_trig.cluster_nmin); ADD_TO_STRING;
	  for(i=0;i<7;i++)
	    {
	      sprintf(sss, "VTP_HPS_FEE_PRESCALE %d %d %d %d\n", i, vtpConf.hps.fee_trig.prescale_xmin[i], vtpConf.hps.fee_trig.prescale_xmax[i], vtpConf.hps.fee_trig.prescale[i]); ADD_TO_STRING;
	    }

	  sprintf(sss, "VTP_HPS_LATENCY %d\n", vtpConf.hps.trig.latency); ADD_TO_STRING;
	  for(i=0;i<32;i++)
	    {
	      sprintf(sss, "VTP_HPS_PRESCALE %d %d\n", i, vtpConf.hps.trig.prescale[i]); ADD_TO_STRING;
	    }
	}

    if(vtpConf.fw_type[0] == VTP_FW_TYPE_COMPTON)
    {
      sprintf(sss, "VTP_COMTPON_VETROC_WIDTH %d\n",
		  vtpConf.compton.vetroc_width); ADD_TO_STRING;

      sprintf(sss, "VTP_COMPTON_LATENCY %d\n",
		  vtpConf.compton.trig.latency); ADD_TO_STRING;

      sprintf(sss, "VTP_COMPTON_WIDTH %d\n",
		  vtpConf.compton.trig.width); ADD_TO_STRING;

      sprintf(sss, "VTP_COMPTON_SCALER_READOUT_EN %d\n",
		  vtpConf.compton.enable_scaler_readout); ADD_TO_STRING;

      for(i=0;i<5;i++)
      {
        sprintf(sss, "VTP_COMTPON_FADC_THRESHOLD %d %d\n",
		  i, vtpConf.compton.fadc_threshold[i]); ADD_TO_STRING;

        sprintf(sss, "VTP_COMPTON_EPLANE_MULT_MIN %d %d\n",
		  i, vtpConf.compton.eplane_mult_min[i]); ADD_TO_STRING;

        sprintf(sss, "VTP_COMPTON_PRESCALE %d %d\n",
	      i, vtpConf.compton.trig.prescale[i]); ADD_TO_STRING;

        sprintf(sss, "VTP_COMPTON_DELAY %d %d\n",
	      i, vtpConf.compton.trig.delay[i]); ADD_TO_STRING;

		sprintf(sss, "VTP_COMPTON_EPLANE_MASK %d %d %d %d %d\n",
	      i, (vtpConf.compton.eplane_mask[i]>>0)&0x1,
             (vtpConf.compton.eplane_mask[i]>>1)&0x1,
             (vtpConf.compton.eplane_mask[i]>>2)&0x1,
             (vtpConf.compton.eplane_mask[i]>>3)&0x1); ADD_TO_STRING;

		sprintf(sss, "VTP_COMPTON_FADC_EN_MASK %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
	      i, (vtpConf.compton.fadc_mask[i]>>0)&0x1,
             (vtpConf.compton.fadc_mask[i]>>1)&0x1,
             (vtpConf.compton.fadc_mask[i]>>2)&0x1,
             (vtpConf.compton.fadc_mask[i]>>3)&0x1,
             (vtpConf.compton.fadc_mask[i]>>4)&0x1,
             (vtpConf.compton.fadc_mask[i]>>5)&0x1,
             (vtpConf.compton.fadc_mask[i]>>6)&0x1,
             (vtpConf.compton.fadc_mask[i]>>7)&0x1,
             (vtpConf.compton.fadc_mask[i]>>8)&0x1,
             (vtpConf.compton.fadc_mask[i]>>9)&0x1,
             (vtpConf.compton.fadc_mask[i]>>10)&0x1,
             (vtpConf.compton.fadc_mask[i]>>11)&0x1,
             (vtpConf.compton.fadc_mask[i]>>12)&0x1,
             (vtpConf.compton.fadc_mask[i]>>13)&0x1,
             (vtpConf.compton.fadc_mask[i]>>14)&0x1,
             (vtpConf.compton.fadc_mask[i]>>15)&0x1
		); ADD_TO_STRING;
      }
    }

    if((vtpConf.fw_type[0] == VTP_FW_TYPE_FADCSTREAM))
    {
      sprintf(sss, "VTP_STREAMING_ROCID %d\n", vtpConf.fadc_streaming.roc_id); ADD_TO_STRING;
      sprintf(sss, "VTP_STREAMING_NFRAME_BUF %d\n", vtpConf.fadc_streaming.nframe_buf); ADD_TO_STRING;
      sprintf(sss, "VTP_STREAMING_FRAMELEN %d\n", vtpConf.fadc_streaming.frame_len); ADD_TO_STRING;
      for(inst=0;inst<2;inst++)
      {
        sprintf(sss, "\nVTP_STREAMING Link %d:\n", inst); ADD_TO_STRING;
        sprintf(sss, "VTP_STREAMING_SLOT_EN %d %d %d %d %d %d %d %d\n",
            (vtpConf.fadc_streaming.eb[inst].mask_en>>0) & 0x1,
            (vtpConf.fadc_streaming.eb[inst].mask_en>>1) & 0x1,
            (vtpConf.fadc_streaming.eb[inst].mask_en>>2) & 0x1,
            (vtpConf.fadc_streaming.eb[inst].mask_en>>3) & 0x1,
            (vtpConf.fadc_streaming.eb[inst].mask_en>>4) & 0x1,
            (vtpConf.fadc_streaming.eb[inst].mask_en>>5) & 0x1,
            (vtpConf.fadc_streaming.eb[inst].mask_en>>6) & 0x1,
            (vtpConf.fadc_streaming.eb[inst].mask_en>>7) & 0x1
          ); ADD_TO_STRING;
        sprintf(sss, "VTP_STREAMING_NSTREAMS %d\n", vtpConf.fadc_streaming.eb[inst].nstreams); ADD_TO_STRING;
        sprintf(sss, "VTP_STREAMING_CONNECT %d\n", vtpConf.fadc_streaming.eb[inst].connect); ADD_TO_STRING;
        sprintf(sss, "VTP_STREAMING_IPADDR %d %d %d %d\n",
            vtpConf.fadc_streaming.eb[inst].ipaddr[0],
            vtpConf.fadc_streaming.eb[inst].ipaddr[1],
            vtpConf.fadc_streaming.eb[inst].ipaddr[2],
            vtpConf.fadc_streaming.eb[inst].ipaddr[3]
          ); ADD_TO_STRING;
        sprintf(sss, "VTP_STREAMING_SUBNET %d %d %d %d\n",
            vtpConf.fadc_streaming.eb[inst].subnet[0],
            vtpConf.fadc_streaming.eb[inst].subnet[1],
            vtpConf.fadc_streaming.eb[inst].subnet[2],
            vtpConf.fadc_streaming.eb[inst].subnet[3]
          ); ADD_TO_STRING;
        sprintf(sss, "VTP_STREAMING_GATEWAY %d %d %d %d\n",
            vtpConf.fadc_streaming.eb[inst].gateway[0],
            vtpConf.fadc_streaming.eb[inst].gateway[1],
            vtpConf.fadc_streaming.eb[inst].gateway[2],
            vtpConf.fadc_streaming.eb[inst].gateway[3]
          ); ADD_TO_STRING;
        sprintf(sss, "VTP_STREAMING_MAC %d %d %d %d %d %d\n",
            vtpConf.fadc_streaming.eb[inst].mac[0],
            vtpConf.fadc_streaming.eb[inst].mac[1],
            vtpConf.fadc_streaming.eb[inst].mac[2],
            vtpConf.fadc_streaming.eb[inst].mac[3],
            vtpConf.fadc_streaming.eb[inst].mac[4],
            vtpConf.fadc_streaming.eb[inst].mac[5]
          ); ADD_TO_STRING;
        sprintf(sss, "VTP_STREAMING_DESTIP %d %d %d %d\n",
            vtpConf.fadc_streaming.eb[inst].destip[0],
            vtpConf.fadc_streaming.eb[inst].destip[1],
            vtpConf.fadc_streaming.eb[inst].destip[2],
            vtpConf.fadc_streaming.eb[inst].destip[3]
          ); ADD_TO_STRING;
        sprintf(sss, "VTP_STREAMING_DESTIPPORT %d\n",
            vtpConf.fadc_streaming.eb[inst].destipport
	  ); ADD_TO_STRING;
        sprintf(sss, "VTP_STREAMING_LOCALPORT %d\n",
            vtpConf.fadc_streaming.eb[inst].localport
	  ); ADD_TO_STRING;
      }
    }

    sprintf(sss,"\n"); ADD_TO_STRING;
    CLOSE_STRING;
  }
  return(0);
}

void
vtpMon()
{
  return;
}
