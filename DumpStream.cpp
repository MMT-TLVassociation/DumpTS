#include "StdAfx.h"
#include "PayloadBuf.h"
#include <memory.h>
#include <string.h>

using namespace std;

extern const char *dump_msg[];
extern unordered_map<std::string, std::string> g_params;
extern TS_FORMAT_INFO g_ts_fmtinfo;


// For MLP audio
#define FBB_SYNC_CODE	0xF8726FBB
#define FBA_SYNC_CODE	0xF8726FBA
#define MLP_SIGNATURE	0xB752

enum CHANNEL_MAP_LOC
{
	CH_LOC_LEFT = 0,
	CH_LOC_CENTER,
	CH_LOC_RIGHT,
	CH_LOC_LS,
	CH_LOC_RS,
	CH_LOC_LC,
	CH_LOC_RC,
	CH_LOC_LRS,
	CH_LOC_RRS,
	CH_LOC_CS,
	CH_LOC_TS,
	CH_LOC_LSD,
	CH_LOC_RSD,
	CH_LOC_LW,
	CH_LOC_RW,
	CH_LOC_VHL,
	CH_LOC_VHR,
	CH_LOC_VHC,
	CH_LOC_LTS,
	CH_LOC_RTS,
	CH_LOC_LFE2,
	CH_LOC_LFE
};

#define CHANNEL_BITMASK(loc)			(1<<loc)

std::tuple<std::string, std::string, std::string> channel_descs[] = 
{
	{ "L", "Left", "A loudspeaker position behind the screen to the left edge, horizontally, of the screen center as viewed from the seating area"},
	{ "C", "Center", ""},
	{ "R", "Right", ""},
	{ "Ls", "Left Surround", ""},
	{ "Rs", "Right Surround", ""},
	{ "Lc", "Left Center", ""},
	{ "Rc", "Right Center", ""},
	{ "Lrs", "Left Rear Surround", ""},
	{ "Rrs", "Right Rear Surround", ""},
	{ "Cs", "Center Surround", ""},
	{ "Ts", "Top Center Surround", ""},
	{ "Lsd", "Left Surround Direct", ""},
	{ "Rsd", "Right Surround Direct", ""},
	{ "Lw", "Left Wide", ""},
	{ "Rw", "Right Wide", ""},
	{ "Vhl", "Vertical Height Left", ""},
	{ "Vhr", "Vertical Height Right", ""},
	{ "Vhc", "Vertical Height Center", ""},
	{ "Lts", "Left Top Surround", ""},
	{ "Rts", "Right Top Surround", ""},
	{ "LFE2", "Secondary low-frequency effects", ""},
	{ "LFE", "Low-frequency effects", ""},
};

std::string GetChannelMappingDesc(unsigned long channel_mapping)
{
	std::string sDesc;
	for (int i = 0; i < sizeof(channel_descs) / sizeof(channel_descs[0]); i++)
	{
		if (channel_mapping&(1 << i))
		{
			if (sDesc.length() > 0)
				sDesc += ", ";
			sDesc += std::get<0>(channel_descs[i]);
		}
	}
	return sDesc;
}

struct AUDIO_INFO
{
	unsigned long	sample_frequency;
	unsigned long	channel_mapping;	// The channel assignment according to bit position defined in CHANNEL_MAP_LOC
	unsigned long	bits_per_sample;
};

struct VIDEO_INFO
{
	unsigned char	EOTF;			// HDR10, SDR
	unsigned char	colorspace;		// REC.601, BT.709 and BT.2020
	unsigned char	colorfmt;		// YUV 4:2:0, 4:2:2 or 4:4:4
	unsigned char	reserved;
	unsigned long	video_height;
	unsigned long	video_width;
	unsigned short	framerate_numerator;
	unsigned short	framerate_denominator;
	unsigned short	aspect_ratio_numerator;
	unsigned short	aspect_ratio_denominator;
};

struct STREAM_INFO
{
	int	stream_coding_type;

	union
	{
		AUDIO_INFO	audio_info;
		VIDEO_INFO	video_info;
	};
};

unordered_map<unsigned short, STREAM_INFO>	g_stream_infos;

int ParseMLPAU(unsigned short PID, int stream_type, unsigned long sync_code, unsigned char* pBuf, int cbSize, STREAM_INFO& audio_info)
{
	if (sync_code != FBA_SYNC_CODE && sync_code != FBB_SYNC_CODE)
		return -1;

	unsigned char check_nibble = (pBuf[0] & 0xF0) >> 4;
	unsigned short access_unit_length = ((pBuf[0] << 8) | pBuf[1]) & 0xFFF;

	audio_info.stream_coding_type = stream_type;

	unsigned char audio_sampling_frequency;
	if (sync_code == FBA_SYNC_CODE)
	{
		audio_info.audio_info.bits_per_sample = 24;

		audio_sampling_frequency = (pBuf[8] >> 4) & 0xF;

		unsigned char channel_assignment_6ch_presentation = ((pBuf[9] & 0x0F) << 1) | ((pBuf[10] >> 7) & 0x01);
		unsigned char channel_assignment_8ch_presentation = ((pBuf[10] & 0x1F) << 8) | pBuf[11];

		audio_info.audio_info.channel_mapping = 0;

		if (channel_assignment_8ch_presentation & 0x01)
			audio_info.audio_info.channel_mapping |= (CHANNEL_BITMASK(CH_LOC_LEFT) | CHANNEL_BITMASK(CH_LOC_RIGHT));

		if (channel_assignment_8ch_presentation & 0x02)
			audio_info.audio_info.channel_mapping |= (CHANNEL_BITMASK(CH_LOC_CENTER));

		if (channel_assignment_8ch_presentation & 0x04)
			audio_info.audio_info.channel_mapping |= (CHANNEL_BITMASK(CH_LOC_LFE));

		if (channel_assignment_8ch_presentation & 0x08)
			audio_info.audio_info.channel_mapping |= (CHANNEL_BITMASK(CH_LOC_LS) | CHANNEL_BITMASK(CH_LOC_RS));

		unsigned short flags = (pBuf[14] << 8) | pBuf[15];
		if ((flags & 0x8000))
		{
			if (channel_assignment_8ch_presentation & 0x10)
				audio_info.audio_info.channel_mapping |= (CHANNEL_BITMASK(CH_LOC_LTS) | CHANNEL_BITMASK(CH_LOC_RTS));
		}
		else
		{
			if (channel_assignment_8ch_presentation & 0x10)
				audio_info.audio_info.channel_mapping |= (CHANNEL_BITMASK(CH_LOC_VHL) | CHANNEL_BITMASK(CH_LOC_VHR));

			if (channel_assignment_8ch_presentation & 0x20)
				audio_info.audio_info.channel_mapping |= (CHANNEL_BITMASK(CH_LOC_LC) | CHANNEL_BITMASK(CH_LOC_RC));

			if (channel_assignment_8ch_presentation & 0x40)
				audio_info.audio_info.channel_mapping |= (CHANNEL_BITMASK(CH_LOC_LRS) | CHANNEL_BITMASK(CH_LOC_RRS));

			if (channel_assignment_8ch_presentation & 0x80)
				audio_info.audio_info.channel_mapping |= (CHANNEL_BITMASK(CH_LOC_CS));

			if (channel_assignment_8ch_presentation & 0x100)
				audio_info.audio_info.channel_mapping |= (CHANNEL_BITMASK(CH_LOC_TS));

			if (channel_assignment_8ch_presentation & 0x200)
				audio_info.audio_info.channel_mapping |= (CHANNEL_BITMASK(CH_LOC_LSD) | CHANNEL_BITMASK(CH_LOC_RSD));

			if (channel_assignment_8ch_presentation & 0x400)
				audio_info.audio_info.channel_mapping |= (CHANNEL_BITMASK(CH_LOC_LW) | CHANNEL_BITMASK(CH_LOC_RW));

			if (channel_assignment_8ch_presentation & 0x800)
				audio_info.audio_info.channel_mapping |= (CHANNEL_BITMASK(CH_LOC_VHC));

			if (channel_assignment_8ch_presentation & 0x1000)
				audio_info.audio_info.channel_mapping |= (CHANNEL_BITMASK(CH_LOC_LFE2));
		}
	}
	else if (sync_code == FBB_SYNC_CODE)
	{
		unsigned char quantization_word_length_1 = (pBuf[8] >> 4);
		unsigned char audio_sampling_frequency_1 = (pBuf[9] >> 4) & 0xF;
		audio_sampling_frequency = audio_sampling_frequency_1;

		audio_info.audio_info.channel_mapping = 0;
		static unsigned long FBB_Channel_assignments[] = {
			/* 00000b(0x00)  */CHANNEL_BITMASK(CH_LOC_CENTER),
			/* 00001b(0x01)o */CHANNEL_BITMASK(CH_LOC_LEFT) | CHANNEL_BITMASK(CH_LOC_RIGHT),
			/* 00010b(0x02)  */(unsigned long)-1,
			/* 00011b(0x03)o */CHANNEL_BITMASK(CH_LOC_LEFT) | CHANNEL_BITMASK(CH_LOC_RIGHT) | CHANNEL_BITMASK(CH_LOC_LS) | CHANNEL_BITMASK(CH_LOC_RS),
			/* 00100b(0x04)o */CHANNEL_BITMASK(CH_LOC_LEFT) | CHANNEL_BITMASK(CH_LOC_RIGHT) | CHANNEL_BITMASK(CH_LOC_LFE),
			/* 00101b(0x05)  */(unsigned long)-1,
			/* 00110b(0x06)o */CHANNEL_BITMASK(CH_LOC_LEFT) | CHANNEL_BITMASK(CH_LOC_RIGHT) | CHANNEL_BITMASK(CH_LOC_LFE) | CHANNEL_BITMASK(CH_LOC_LS) | CHANNEL_BITMASK(CH_LOC_RS),
			/* 00111b(0x07)o */CHANNEL_BITMASK(CH_LOC_LEFT) | CHANNEL_BITMASK(CH_LOC_RIGHT) | CHANNEL_BITMASK(CH_LOC_CENTER),
			/* 01000b(0x08)  */(unsigned long)-1,
			/* 01001b(0x09)  */CHANNEL_BITMASK(CH_LOC_LEFT) | CHANNEL_BITMASK(CH_LOC_RIGHT) | CHANNEL_BITMASK(CH_LOC_CENTER) | CHANNEL_BITMASK(CH_LOC_LS) | CHANNEL_BITMASK(CH_LOC_RS),
			/* 01011b(0x0A)o */CHANNEL_BITMASK(CH_LOC_LEFT) | CHANNEL_BITMASK(CH_LOC_RIGHT) | CHANNEL_BITMASK(CH_LOC_CENTER) | CHANNEL_BITMASK(CH_LOC_LFE),
			/* 01010b(0x0B)  */(unsigned long)-1,
			/* 01011b(0x0C)o */CHANNEL_BITMASK(CH_LOC_LEFT) | CHANNEL_BITMASK(CH_LOC_RIGHT) | CHANNEL_BITMASK(CH_LOC_CENTER) | CHANNEL_BITMASK(CH_LOC_LFE) | CHANNEL_BITMASK(CH_LOC_LS) | CHANNEL_BITMASK(CH_LOC_RS),
			/* 01100b(0x0D)  */(unsigned long)-1,
			/* 01110b(0x0E)o */CHANNEL_BITMASK(CH_LOC_LEFT) | CHANNEL_BITMASK(CH_LOC_RIGHT) | CHANNEL_BITMASK(CH_LOC_CENTER) | CHANNEL_BITMASK(CH_LOC_LS) | CHANNEL_BITMASK(CH_LOC_RS),
			/* 01111b(0x0F)  */CHANNEL_BITMASK(CH_LOC_LEFT) | CHANNEL_BITMASK(CH_LOC_RIGHT) | CHANNEL_BITMASK(CH_LOC_CENTER) | CHANNEL_BITMASK(CH_LOC_LFE),
			/* 10000b(0x10)  */(unsigned long)-1,
			/* 10001b(0x11)  */CHANNEL_BITMASK(CH_LOC_LEFT) | CHANNEL_BITMASK(CH_LOC_RIGHT) | CHANNEL_BITMASK(CH_LOC_CENTER) | CHANNEL_BITMASK(CH_LOC_LFE) | CHANNEL_BITMASK(CH_LOC_LS) | CHANNEL_BITMASK(CH_LOC_RS),
			/* 10010b(0x12)  */CHANNEL_BITMASK(CH_LOC_LEFT) | CHANNEL_BITMASK(CH_LOC_RIGHT) | CHANNEL_BITMASK(CH_LOC_LS) | CHANNEL_BITMASK(CH_LOC_RS) | CHANNEL_BITMASK(CH_LOC_LFE),
			/* 10011b(0x13)  */CHANNEL_BITMASK(CH_LOC_LEFT) | CHANNEL_BITMASK(CH_LOC_RIGHT) | CHANNEL_BITMASK(CH_LOC_LS) | CHANNEL_BITMASK(CH_LOC_RS) | CHANNEL_BITMASK(CH_LOC_CENTER),
			/* 10100b(0x14)  */CHANNEL_BITMASK(CH_LOC_LEFT) | CHANNEL_BITMASK(CH_LOC_RIGHT) | CHANNEL_BITMASK(CH_LOC_LS) | CHANNEL_BITMASK(CH_LOC_RS) | CHANNEL_BITMASK(CH_LOC_CENTER) | CHANNEL_BITMASK(CH_LOC_LFE) ,
		};
		
		unsigned char channel_assignment = pBuf[11] & 0x1F;
		if (channel_assignment < sizeof(FBB_Channel_assignments) / sizeof(FBB_Channel_assignments[0]))
			audio_info.audio_info.channel_mapping = FBB_Channel_assignments[channel_assignment];
		else
			audio_info.audio_info.channel_mapping = 0;	// Not support
		
		audio_info.audio_info.bits_per_sample = quantization_word_length_1 == 0 ? 16 : (quantization_word_length_1 == 1 ? 20 : (quantization_word_length_1 == 2 ? 24 : 0));
	}

	switch (audio_sampling_frequency)
	{
	case 0x0: audio_info.audio_info.sample_frequency = 48000; break;
	case 0x1: audio_info.audio_info.sample_frequency = 96000; break;
	case 0x2: audio_info.audio_info.sample_frequency = 192000; break;
	case 0x8: audio_info.audio_info.sample_frequency = 44100; break;
	case 0x9: audio_info.audio_info.sample_frequency = 88200; break;
	case 0xA: audio_info.audio_info.sample_frequency = 176400; break;
	default:
		audio_info.audio_info.sample_frequency = (unsigned long)-1;
	}

	return 0;
}

int CheckRawBufferMediaInfo(unsigned short PID, int stream_type, unsigned char* pBuf, int cbSize)
{
	// At first, check whether the stream type is already decided or not for the current dumped stream.
	if (stream_type == DOLBY_LOSSLESS_AUDIO_STREAM)
	{
		// Try to analyze the MLP audio information.
		// header size:
		// check_nibble/access_unit_length/input_timing: 4 bytes
		// major_sync_info: at least 28 bytes
		const int min_header_size = 32;
		if (cbSize < min_header_size)
			return -1;

		unsigned char* p = pBuf;
		int cbLeft = cbSize;
		unsigned long sync_code = (p[4] << 16) | (p[5] << 8) | p[6];
		// For the first round, try to find FBA sync code
		while (cbLeft >= min_header_size)
		{
			sync_code = (sync_code << 8) | p[7];
			if (sync_code == FBA_SYNC_CODE || sync_code == FBB_SYNC_CODE)
			{
				int signature = (p[12] << 8) + p[13];

				if (signature == MLP_SIGNATURE)
				{
					/* found it */
					break;
				}
			}
			
			cbLeft--;
			p++;
		}

		if (cbLeft >= min_header_size && (sync_code == FBA_SYNC_CODE || sync_code == FBB_SYNC_CODE))
		{
			STREAM_INFO stm_info;
			if (ParseMLPAU(PID, stream_type, sync_code, p, cbLeft, stm_info) == 0)
			{
				// Compare with the previous audio information.
				bool bChanged = false;
				if (g_stream_infos.find(PID) == g_stream_infos.end())
					bChanged = true;
				else
				{
					STREAM_INFO& cur_stm_info = g_stream_infos[PID];
					if (memcmp(&cur_stm_info, &stm_info, sizeof(stm_info)) != 0)
						bChanged = true;
				}

				if (bChanged)
				{
					g_stream_infos[PID] = stm_info;
					printf("MLP Audio Stream information:\r\n");
					printf("\tPID: 0X%X.\r\n", PID);
					printf("\tStream Type: %d.\r\n", stm_info.stream_coding_type);
					printf("\tSample Frequency: %d (HZ).\r\n", stm_info.audio_info.sample_frequency);
					printf("\tBits Per Sample: %d.\r\n", stm_info.audio_info.bits_per_sample);
					printf("\tChannel Layout: %s.\r\n", GetChannelMappingDesc(stm_info.audio_info.channel_mapping).c_str());
				}
			}
		}
	}

	return -1;
}

int FlushPSIBuffer(FILE* fw, unsigned char* psi_buffer, int psi_buffer_len, int dumpopt, int &raw_data_len)
{
	int iret = 0;
	// For PSI, store whole payload with pointer field
	raw_data_len = psi_buffer_len;
	if (fw != NULL)
		fwrite(psi_buffer, 1, raw_data_len, fw);

	return iret;
}

int FlushPESBuffer(FILE* fw, unsigned short PID, int stream_type, unsigned char* pes_buffer, int pes_buffer_len, int dumpopt, int &raw_data_len, int stream_id = -1, int stream_id_extension = -1)
{
	int iret = 0;
	raw_data_len = 0;
	if (pes_buffer_len >= 9)
	{
		if (pes_buffer[0] == 0 &&
			pes_buffer[1] == 0 &&
			pes_buffer[2] == 1)
		{
			int off = 9;
			int pes_stream_id = pes_buffer[3];
			int pes_stream_id_extension = -1;
			int pes_len = pes_buffer[4] << 8 | pes_buffer[5];
			int pes_hdr_len = pes_buffer[8];
			unsigned char PTS_DTS_flags = (pes_buffer[7] >> 6) & 0x3;
			unsigned char ESCR_flag = (pes_buffer[7] >> 5) & 0x1;
			unsigned char ES_rate_flag = (pes_buffer[7] >> 4) & 0x1;
			unsigned char DSM_trick_mode_flag = (pes_buffer[7] >> 3) & 0x1;
			unsigned char additional_copy_info_flag = (pes_buffer[7] >> 2) & 0x1;
			unsigned char PES_CRC_flag = (pes_buffer[7] >> 1) & 0x1;
			unsigned char pes_hdr_extension_flag = pes_buffer[7] & 0x1;

			if (PTS_DTS_flags == 2)
				off += 5;
			else if (PTS_DTS_flags == 3)
				off += 10;

			if (ESCR_flag)
				off += 6;

			if (ES_rate_flag)
				off += 3;

			if (DSM_trick_mode_flag)
				off += 1;

			if (additional_copy_info_flag)
				off += 1;

			if (PES_CRC_flag)
				off += 2;

			if (off >= pes_buffer_len)
				return -1;

			if (pes_hdr_extension_flag)
			{
				unsigned char PES_private_data_flag = (pes_buffer[off] >> 7) & 0x1;
				unsigned char pack_header_field_flag = (pes_buffer[off] >> 6) & 0x1;
				unsigned char program_packet_sequence_counter_flag = (pes_buffer[off] >> 5) & 0x1;
				unsigned char PSTD_buffer_flag = (pes_buffer[off] >> 4) & 0x1;
				unsigned char PES_extension_flag_2 = pes_buffer[off] & 0x1;
				off += 1;

				if (PES_private_data_flag)
					off += 16;

				if (pack_header_field_flag)
				{
					off++;

					if (off >= pes_buffer_len)
						return -1;

					off += pes_buffer[off];
				}

				if (program_packet_sequence_counter_flag)
					off += 2;

				if (PSTD_buffer_flag)
					off += 2;

				if (off >= pes_buffer_len)
					return -1;

				if (PES_extension_flag_2)
				{
					unsigned char PES_extension_field_length = pes_buffer[off] & 0x7F;

					off++;

					if (off >= pes_buffer_len)
						return -1;

					if (PES_extension_field_length > 0)
					{
						unsigned char stream_id_extension_flag = (pes_buffer[off] >> 7) & 0x1;
						if (stream_id_extension_flag == 0)
						{
							pes_stream_id_extension = pes_buffer[off];
						}
					}
				}
			}

			// filter it by stream_id and stream_id_extension
			if (stream_id != -1 && stream_id != pes_stream_id)
				return 1;	// mis-match with stream_id filter

			if (stream_id_extension != -1 && pes_stream_id_extension != -1 && stream_id_extension != pes_stream_id_extension)
				return 1;	// mis-match with stream_id filter

			// Check the media information of current elementary stream
			if (g_params.find("showinfo") != g_params.end())
			{
				if (pes_len == 0)
				{
					raw_data_len = pes_buffer_len - pes_hdr_len - 9;
					CheckRawBufferMediaInfo(PID, stream_type, pes_buffer + pes_hdr_len + 9, raw_data_len);
				}
				else
				{
					raw_data_len = pes_len - 3 - pes_hdr_len;
					CheckRawBufferMediaInfo(PID, stream_type, pes_buffer + pes_hdr_len + 9, raw_data_len);
				}
			}

			if (dumpopt&DUMP_RAW_OUTPUT)
			{
				if (pes_buffer_len < pes_len + 6 || pes_buffer_len < pes_hdr_len + 9)
				{
					iret = -1;
				}
				else if (pes_len == 0)
				{
					raw_data_len = pes_buffer_len - pes_hdr_len - 9;
					if (fw != NULL)
						fwrite(pes_buffer + pes_hdr_len + 9, 1, raw_data_len, fw);
				}
				else
				{
					raw_data_len = pes_len - 3 - pes_hdr_len;
					if (fw != NULL)
						fwrite(pes_buffer + pes_hdr_len + 9, 1, raw_data_len, fw);
				}
			}
			else if (dumpopt&DUMP_PES_OUTPUT)
			{
				raw_data_len = pes_len == 0 ? pes_buffer_len : pes_len;
				if (fw != NULL)
					fwrite(pes_buffer, 1, raw_data_len, fw);
			}

			if (dumpopt&DUMP_PTS_VIEW)
			{
				if (pes_buffer_len < pes_len + 6 || pes_buffer_len < pes_hdr_len + 9)
				{
					iret = -1;
				}
				else
				{
					static int PktIndex = 0;
					__int64 pts = GetPTSValue(pes_buffer + 9);
					printf("PktIndex:%d, PTS value is %lld(0X%llX).\r\n", PktIndex++, pts, pts);
				}
			}
		}
		else
		{
			iret = -2;	// invalid ES
		}
	}
	else if (pes_buffer_len > 0)
	{
		iret = -3;	// too short PES raw data buffer
	}

	return iret;
}

// Dump a stream with specified PID to a pes/es stream
int DumpOneStream()
{
	int iRet = -1;
	FILE *fp = NULL, *fw = NULL;
	int sPID = FILTER_PID;
	int dumpopt = 0;
	unsigned char* pes_buffer = new (std::nothrow) unsigned char[20 * 1024 * 1024];

	unordered_map<unsigned short, CPayloadBuf*> pPSIBufs;
	unordered_map<unsigned short, unsigned char> stream_types;

	pPSIBufs[0] = new CPayloadBuf(PID_PROGRAM_ASSOCIATION_TABLE);

	unsigned char buf[1024];

	if (g_params.find("pid") == g_params.end())
	{
		printf("No PID is specified, nothing to do.\r\n");
		goto done;
	}

	sPID = (int)ConvertToLongLong(g_params["pid"]);

	errno_t errn = fopen_s(&fp, g_params["input"].c_str(), "rb");
	if (errn != 0 || fp == NULL)
	{
		printf("Failed to open the file: %s {errno: %d}.\r\n", g_params["input"].c_str(), errn);
		goto done;
	}

	if (g_params.find("output") != g_params.end())
	{
		errn = fopen_s(&fw, g_params["output"].c_str(), "wb+");
		if (errn != 0 || fw == NULL)
		{
			printf("Failed to open the file: %s {errno: %d}.\r\n", g_params["output"].c_str(), errn);
			goto done;
		}
	}

	size_t pes_hdr_location;
	int raw_data_len, dump_ret;
	unsigned long pes_buffer_len = 0;
	unsigned long pat_buffer_len = 0;
	int buf_head_offset = g_ts_fmtinfo.num_of_prefix_bytes;
	int ts_pack_size = g_ts_fmtinfo.packet_size;

	if (g_params.find("srcfmt") != g_params.end() && g_params["srcfmt"].compare("m2ts") == 0)
		dumpopt |= DUMP_BD_M2TS;

	int stream_id_extension = -1;
	if (g_params.find("stream_id_extension") != g_params.end())
	{
		stream_id_extension = (int)ConvertToLongLong(g_params["stream_id_extension"]);
	}

	int stream_id = -1;
	if (g_params.find("stream_id") != g_params.end())
	{
		stream_id = (int)ConvertToLongLong(g_params["stream_id"]);
	}

	// Make sure the dump option
	if (g_params.find("outputfmt") != g_params.end())
	{
		if (g_params["outputfmt"].compare("es") == 0)
			dumpopt |= DUMP_RAW_OUTPUT;
		else if (g_params["output"].compare("pes") == 0)
			dumpopt |= DUMP_PES_OUTPUT;
	}

	if (g_params.find("showpts") != g_params.end())
	{
		dumpopt |= DUMP_PTS_VIEW;
	}

	unsigned long ts_pack_idx = 0;
	while (true)
	{
		int nRead = fread(buf, 1, ts_pack_size, fp);
		if (nRead < ts_pack_size)
			break;

		unsigned short PID = (buf[buf_head_offset + 1] & 0x1f) << 8 | buf[buf_head_offset + 2];

		bool bPSI = pPSIBufs.find(PID) != pPSIBufs.end() ? true : false;

		// Continue the parsing when PAT/PMT is hit
		if (PID != sPID && !bPSI)
		{
			ts_pack_idx++;
			continue;
		}

		int index = buf_head_offset + 4;
		unsigned char payload_unit_start = buf[buf_head_offset + 1] & 0x40;
		unsigned char adaptation_field_control = (buf[buf_head_offset + 3] >> 4) & 0x03;
		unsigned char discontinuity_counter = buf[buf_head_offset + 3] & 0x0f;

		if (adaptation_field_control & 0x02)
			index += buf[buf_head_offset + 4] + 1;

		if (payload_unit_start)
		{
			if (bPSI)
			{
				if (pPSIBufs.find(PID) != pPSIBufs.end())
				{
					if (pPSIBufs[PID]->ProcessPMT(pPSIBufs) == 0)
					{
						// Update each PID stream type
						unsigned char stream_type = 0xFF;
						if (pPSIBufs[PID]->GetPMTInfo(sPID, stream_type) == 0)
							stream_types[sPID] = stream_type;
					}

					pPSIBufs[PID]->Reset();
				}
			}

			if (PID == sPID)
			{
				if (bPSI)
				{
					if ((dump_ret = FlushPSIBuffer(fw, pes_buffer, pes_buffer_len, dumpopt, raw_data_len)) < 0)
						printf(dump_msg[-dump_ret - 1], ftell(fp), ftell(fw));
				}
				else
				{
					if ((dump_ret = FlushPESBuffer(fw, 
						PID, stream_types.find(PID) == stream_types.end()?-1: stream_types[PID], 
						pes_buffer, pes_buffer_len, dumpopt, raw_data_len, stream_id, stream_id_extension)) < 0)
						printf(dump_msg[-dump_ret - 1], ftell(fp), ftell(fw));
				}

				pes_buffer_len = 0;
				pes_hdr_location = ftell(fp) - nRead;
			}
		}

		if (bPSI)
		{
			if (pPSIBufs[PID]->PushTSBuf(ts_pack_idx, buf, index, g_ts_fmtinfo.packet_size + g_ts_fmtinfo.num_of_prefix_bytes) >= 0)
			{
				// Try to process PSI buffer
				int nProcessPMTRet = pPSIBufs[PID]->ProcessPMT(pPSIBufs);
				// If process PMT result is -1, it means the buffer is too small, don't reset the buffer.
				if (nProcessPMTRet != -1)
					pPSIBufs[PID]->Reset();

				if (nProcessPMTRet == 0)
				{
					// Update each PID stream type
					unsigned char stream_type = 0xFF;
					if (pPSIBufs[PID]->GetPMTInfo(sPID, stream_type) == 0)
						stream_types[sPID] = stream_type;
				}
			}
		}

		if (PID == sPID && (payload_unit_start || !payload_unit_start && pes_buffer_len > 0))
		{
			memcpy(pes_buffer + pes_buffer_len, buf + index, g_ts_fmtinfo.packet_size + g_ts_fmtinfo.num_of_prefix_bytes - index);
			pes_buffer_len += g_ts_fmtinfo.packet_size + g_ts_fmtinfo.num_of_prefix_bytes - index;
		}

		ts_pack_idx++;
	}

	if (sPID == PID_PROGRAM_ASSOCIATION_TABLE || pPSIBufs.find(sPID) != pPSIBufs.end())
	{
		if ((dump_ret = FlushPSIBuffer(fw, pes_buffer, pes_buffer_len, dumpopt, raw_data_len)) < 0)
			printf(dump_msg[-dump_ret - 1], ftell(fp), ftell(fw));
	}
	else
	{
		if ((dump_ret = FlushPESBuffer(fw, 
			sPID, stream_types.find(sPID) == stream_types.end() ? -1 : stream_types[sPID],
			pes_buffer, pes_buffer_len, dumpopt, raw_data_len, stream_id, stream_id_extension)) < 0)
			printf(dump_msg[-dump_ret - 1], ftell(fp), ftell(fw));
	}

	iRet = 0;

done:
	if (fp)
		fclose(fp);
	if (fw)
		fclose(fw);

	if (pes_buffer != NULL)
	{
		delete[] pes_buffer;
		pes_buffer = NULL;
	}

	for (std::unordered_map<unsigned short, CPayloadBuf*>::iterator iter = pPSIBufs.begin(); iter != pPSIBufs.end(); iter++)
	{
		delete iter->second;
		iter->second = NULL;
	}

	return iRet;
}