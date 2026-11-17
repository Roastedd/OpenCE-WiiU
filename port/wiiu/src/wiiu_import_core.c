/* First-run Xbox map conversion, using the same schema walk and relocation
rules as tools/wiiu/convert_maps.py. Called before the game's heaps exist. */
typedef unsigned char boolean;
typedef unsigned char byte;
#include "../../linux/game/tag_swap.h"
#include "wiiu_import_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../../third_party/zlib/zlib_prefixed.h"

#define IMPORT_HEADER 0x800u
#define IMPORT_MAXIMUM (384u * 1024u * 1024u)
#define IMPORT_CHUNK 65536u
#define IMPORT_REBASE 0xC1EA0000UL

static unsigned long read_little(byte const *bytes)
{
	return (unsigned long)bytes[0] | (unsigned long)bytes[1] << 8 |
		(unsigned long)bytes[2] << 16 | (unsigned long)bytes[3] << 24;
}

int xbox_map_import_file(char const *source, char const *destination,
	xbox_map_import_progress progress, void *context, char *error, int error_size)
{
	FILE *input = NULL, *output = NULL;
	byte header[IMPORT_HEADER];
	byte *map = NULL, *chunk = NULL;
	void *scratch = NULL;
	char temporary[1024];
	struct tag_swap_report report;
	z_stream stream;
	unsigned long bytes, source_bytes;
	long length;
	int zstate = 0, ok = 0;
	char const *problem = "Cannot read this Xbox map.";

	temporary[0] = 0;
	memset(&stream, 0, sizeof(stream));
	input = fopen(source, "rb");
	if (!input || fread(header, 1, sizeof(header), input) != sizeof(header) ||
		fseek(input, 0, SEEK_END) != 0 || (length = ftell(input)) < (long)IMPORT_HEADER)
		goto done;
	source_bytes = (unsigned long)length;
	bytes = read_little(header + 8);
	if (read_little(header) != 0x68656164UL || read_little(header + 4) != 5 ||
		bytes < IMPORT_HEADER || bytes > IMPORT_MAXIMUM || source_bytes > IMPORT_MAXIMUM)
	{
		problem = "Not a supported original-Xbox map (maximum inflated size: 384 MiB).";
		goto done;
	}
	map = malloc(bytes);
	chunk = malloc(IMPORT_CHUNK);
	if (!map || !chunk)
	{
		problem = "Not enough memory to prepare this map.";
		goto done;
	}
	memcpy(map, header, IMPORT_HEADER);
	if (fseek(input, IMPORT_HEADER, SEEK_SET) != 0)
		goto done;
	if (source_bytes == bytes)
	{
		unsigned long at = IMPORT_HEADER;

		while (at < bytes)
		{
			unsigned long count = bytes - at > IMPORT_CHUNK ? IMPORT_CHUNK : bytes - at;

			if (progress && progress(context, "Reading", at, bytes))
				goto cancelled;
			if (fread(map + at, 1, count, input) != count)
				goto done;
			at += count;
		}
	}
	else
	{
		int result = Z_OK;

		if (inflateInit(&stream) != Z_OK)
			goto done;
		zstate = 1;
		stream.next_out = map + IMPORT_HEADER;
		stream.avail_out = bytes - IMPORT_HEADER;
		while (result == Z_OK)
		{
			if (progress && progress(context, "Reading", (unsigned long)stream.total_out, bytes))
				goto cancelled;
			stream.avail_in = (uInt)fread(chunk, 1, IMPORT_CHUNK, input);
			stream.next_in = chunk;
			if (!stream.avail_in)
				break;
			while (stream.avail_in && result == Z_OK)
				result = inflate(&stream, Z_NO_FLUSH);
		}
		if (result != Z_STREAM_END || stream.total_out != bytes - IMPORT_HEADER ||
			stream.total_in > source_bytes - IMPORT_HEADER ||
			source_bytes - IMPORT_HEADER - stream.total_in >= IMPORT_HEADER)
		{
			problem = "This compressed Xbox map is incomplete or damaged.";
			goto done;
		}
		/* Retail disc files pad the compressed stream to a 2 KiB boundary. */
		{
			unsigned long index;
			size_t count;

			for (index = 0; index < stream.avail_in; index++)
				if (stream.next_in[index])
					goto done;
			while ((count = fread(chunk, 1, IMPORT_CHUNK, input)) != 0)
				for (index = 0; index < count; index++)
					if (chunk[index])
						goto done;
			if (ferror(input))
				goto done;
		}
		inflateEnd(&stream);
		zstate = 0;
	}
	fclose(input);
	input = NULL;
	if (progress && progress(context, "Converting", 0, bytes))
		goto cancelled;
	scratch = malloc(tag_swap_xbox_rebase_scratch_bytes(bytes));
	if (!scratch)
	{
		problem = "Not enough memory for map conversion.";
		goto done;
	}
	if (!tag_swap_xbox_map_rebased(map, bytes, scratch, &report, IMPORT_REBASE))
	{
		if (error && error_size > 0)
			snprintf(error, (size_t)error_size, "Map conversion refused: %s", report.problem);
		problem = NULL;
		goto done;
	}
	free(scratch);
	scratch = NULL;
	problem = "Cannot write the converted map. Check the SD card's free space.";
	if (snprintf(temporary, sizeof(temporary), "%s.part", destination) >= (int)sizeof(temporary))
		goto done;
	output = fopen(temporary, "wb");
	if (!output || fwrite(map, 1, IMPORT_HEADER, output) != IMPORT_HEADER)
		goto done;
	memset(&stream, 0, sizeof(stream));
	if (deflateInit(&stream, 6) != Z_OK)
		goto done;
	zstate = 2;
	{
		unsigned long at = IMPORT_HEADER;
		int result = Z_OK;

		while (result != Z_STREAM_END)
		{
			if (progress && progress(context, "Writing", at, bytes))
				goto cancelled;
			if (!stream.avail_in && at < bytes)
			{
				unsigned long count = bytes - at > IMPORT_CHUNK ? IMPORT_CHUNK : bytes - at;

				stream.next_in = map + at;
				stream.avail_in = count;
				at += count;
			}
			stream.next_out = chunk;
			stream.avail_out = IMPORT_CHUNK;
			result = deflate(&stream, at == bytes ? Z_FINISH : Z_NO_FLUSH);
			if ((result != Z_OK && result != Z_STREAM_END) ||
				fwrite(chunk, 1, IMPORT_CHUNK - stream.avail_out, output) != IMPORT_CHUNK - stream.avail_out)
				goto done;
		}
	}
	deflateEnd(&stream);
	zstate = 0;
	if (fflush(output) != 0 || fsync(fileno(output)) != 0)
		goto done;
	{
		int closed = fclose(output);

		output = NULL;
		if (closed || rename(temporary, destination) != 0)
			goto done;
	}
	ok = 1;
	goto done;

cancelled:
	problem = "Map preparation cancelled. You can try again next time.";
done:
	if (zstate == 1)
		inflateEnd(&stream);
	if (zstate == 2)
		deflateEnd(&stream);
	if (input)
		fclose(input);
	if (output)
		fclose(output);
	if (!ok && temporary[0])
		remove(temporary);
	free(scratch);
	free(chunk);
	free(map);
	if (!ok && problem && error && error_size > 0)
		snprintf(error, (size_t)error_size, "%s", problem);
	return ok;
}
