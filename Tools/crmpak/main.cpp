#include <inttypes.h>
#include <stdio.h>
#include "game/room_file.h"
#include "util/data_ext.h"
#include "util/file.h"
#include "util/memorystream.h"
#include "util/string_compat.h"

using namespace AGS::Common;


// TODO: move to Common? need to find a good place
class RoomBlockParser : public DataExtParser
{
public:
    RoomBlockParser(Stream *in, RoomFileVersion data_ver)
        : DataExtParser(in, kDataExt_NumID8 | ((data_ver < kRoomVersion_350) ? kDataExt_File32 : kDataExt_File64))
        {}
    virtual String GetOldBlockName(int block_id) const
    { return GetRoomBlockName((RoomFileBlock)block_id); }
};


HError print_room_blockids(RoomDataSource &datasrc)
{
    HError err = HError::None();
    RoomBlockParser parser(datasrc.InputStream.get(), datasrc.DataVersion);
    printf("------ Block ID ------|------- Offset -------|--- Size --\n");
    for (err = parser.OpenBlock(); err && !parser.AtEnd(); err = parser.OpenBlock())
    {
        printf(" %-16s (%d) | %-20" PRId64 " | %-10zu\n",
            parser.GetBlockName().GetCStr(), parser.GetBlockID(), parser.GetBlockOffset(), (size_t)parser.GetBlockLength());
        parser.SkipBlock();
    }
    return err;
}


const char *HELP_STRING =
"Usage: crmpak <in-room.crm> <COMMAND> [<OPTIONS>]\n"
"Commands:\n"
"  -d <blockid>           delete: remove a block from the compiled room\n"
"  -e <blockid> <file>    export: write a block into this file\n"
"  -i <blockid> <file>    import: add/replace a block with this file contents\n"
"  -l                     list: print id of all blocks found in the room\n"
"  -x <blockid> <file>    extract: remove a block and save it in this file\n"
"Options:\n"
"  -w <out-room.crm>      for all commands but '-e': write the resulting room\n"
"                         into a new file; otherwise will modify the input file\n";

int main(int argc, char *argv[])
{
    printf("crmpak v0.1.0 - AGS compiled room's (re)packer\n"\
        "Copyright (c) 2021 AGS Team and contributors\n");
    for (int i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];
        if (ags_stricmp(arg, "--help") == 0 || ags_stricmp(arg, "/?") == 0 || ags_stricmp(arg, "-?") == 0)
        {
            printf("%s\n", HELP_STRING);
            return 0; // display help and bail out
        }
    }

    const char *in_roomfile = argv[1];
    char command = 0;
    const char *arg_block = nullptr;
    const char *arg_blockfile = nullptr;
    const char *out_roomfile = nullptr;
    for (int i = 2; i < argc; ++i)
    {
        if (argv[i][0] != '-' || strlen(argv[i]) != 2)
            continue;
        char arg = argv[2][1];
        switch (arg)
        {
        case 'e': case 'i': case 'x':
            command = arg;
            if (argc > i + 2)
            {
                arg_block = argv[++i];
                arg_blockfile = argv[++i];
            }
            break;
        case 'd':
            command = arg;
            if (argc > i + 1) arg_block = argv[++i];
            break;
        case 'w':
            if (argc > i + 1) out_roomfile = argv[(i++) + 1];
            break;
        case 'l': command = arg; break;
        }
    }

    // Test supported commands and number of args
    if (command == 0)
    {
        printf("Error: command not specified\n");
        printf("%s\n", HELP_STRING);
        return -1;
    }
    else if ((command != 'l') &&
        (!arg_block || ((command != 'd') && !arg_blockfile)))
    {
        printf("Error: not enough arguments\n");
        printf("%s\n", HELP_STRING);
        return -1;
    }

    // Print working info
    int block_numid = 0;
    String block_strid;
    printf("Room file: %s\n", in_roomfile);
    if (command != 'l')
    {
        // Parse room block ID
        char *parse_end = nullptr;
        errno = 0;
        block_numid = strtol(arg_block, &parse_end, 0);
        bool is_old_numid = ((errno == 0) && (parse_end == arg_block + strlen(arg_block)));
        block_strid = is_old_numid ? GetRoomBlockName((RoomFileBlock)block_numid) : arg_block;

        printf("Block ID: %s (%d)\n", block_strid.GetCStr(), block_numid);
        switch (command)
        {
        case 'e': case 'x': printf("Output file: %s\n", arg_blockfile); break;
        case 'i': printf("Input file: %s\n", arg_blockfile); break;
        case 'd': default: break;
        }
        if (out_roomfile && (command != 'e'))
            printf("Write modified room into: %s\n", out_roomfile);
    }

    //-----------------------------------------------------------------------//
    // Open the room, export list of block ids ('l' command).
    //-----------------------------------------------------------------------//
    RoomDataSource datasrc;
    HError err = OpenRoomFile(in_roomfile, datasrc);
    if (!err)
    {
        printf("Error: failed to open room file for reading:\n");
        printf("%s\n", err->FullMessage().GetCStr());
        return -1;
    }

    if (command == 'l')
    {
        HError err = print_room_blockids(datasrc);
        if (!err)
        {
            printf("Error: failed to parse the input room:\n");
            printf("%s\n", err->FullMessage().GetCStr());
            return -1;
        }
        return 0;
    }

    //-----------------------------------------------------------------------//
    // Parse the input room, search for the requested block ID;
    // save its location in the stream.
    //-----------------------------------------------------------------------//
    RoomBlockParser parser(datasrc.InputStream.get(), datasrc.DataVersion);
    soff_t block_head = -1;
    soff_t block_data_at = -1;
    soff_t block_end = -1;
    for (block_end = parser.GetStream()->GetPosition(), err = parser.OpenBlock();
         (block_head < 0) && err && !parser.AtEnd(); err = parser.OpenBlock())
    {
        if (parser.GetBlockID() == block_numid || parser.GetBlockName() == block_strid)
        {
            block_head = block_end;
            block_data_at = parser.GetStream()->GetPosition();
        }
        parser.SkipBlock();
        block_end = parser.GetStream()->GetPosition();
    }
    // need these later
    const int dataext_flags = parser.GetFlags();
    const RoomFileVersion dataver = datasrc.DataVersion;
        
    if (!err)
    {
        printf("Error: failed to parse the input room:\n");
        printf("%s\n", err->FullMessage().GetCStr());
        return -1;
    }
    // If no block found for deletion / export - stop
    if ((block_head < 0) && (command != 'i'))
    {
        printf("Requested block not found.\n");
        return 0;
    }

    //-----------------------------------------------------------------------//
    // Export the block data (commands 'e' and 'x')
    //-----------------------------------------------------------------------//
    if (command == 'e' || command == 'x')
    {
        std::unique_ptr<Stream> block_out(File::CreateFile(arg_blockfile));
        if (!block_out)
        {
            printf("Error: failed to open block file for writing.\n");
            return -1;
        }
        // Note we export only the internal block data, skipping the header
        datasrc.InputStream->Seek(block_data_at, kSeekBegin);
        CopyStream(datasrc.InputStream.get(), block_out.get(), block_end - block_data_at);
    }

    // Export is complete - stop
    if (command == 'e')
    {
        printf("Done.\n");
        return 0;
    }

    //-----------------------------------------------------------------------//
    // Write the new room file (commands 'd', 'i', 'x')
    //-----------------------------------------------------------------------//
    // If we are importing, first try opening the input block file
    std::unique_ptr<Stream> block_in;
    if (command == 'i')
    {
        block_in.reset(File::OpenFileRead(arg_blockfile));
        if (!block_in)
        {
            printf("Error: failed to open block file for reading.\n");
            return -1;
        }
    }

    // Depending on settings we write either directly into the new room file,
    // or into the temp buffer which we then use to overwrite existing room
    std::unique_ptr<Stream> room_out;
    std::vector<char> temp_data;
    if (out_roomfile)
    {
        room_out.reset(File::CreateFile(out_roomfile));
        if (!room_out)
        {
            printf("Error: failed to open room file for writing.\n");
            return -1;
        }
    }
    else
    {
        room_out.reset(new MemoryStream(temp_data, kStream_Write));
    }

    // Write whole room, except for the block piece (if found)
    datasrc.InputStream->Seek(0, kSeekBegin);
    CopyStream(datasrc.InputStream.get(), room_out.get(), block_head);
    datasrc.InputStream->Seek(block_end, kSeekBegin);
    CopyStream(datasrc.InputStream.get(), room_out.get(), datasrc.InputStream->GetLength() - block_end);
    // Finally close the room input
    datasrc.InputStream.reset();
    
    // If we are importing, append the new block
    if (command == 'i')
    {
        WriteExtBlock(block_numid, block_strid,
            [&block_in](Stream *out) { CopyStream(block_in.get(), out, block_in->GetLength()); },
            dataext_flags, room_out.get());
        // TODO: find a better design for this block writing ^
        // TODO: also maybe modify CopyStream to support reading until input EOS
        block_in.reset();
    }

    // Finalize the output room
    WriteRoomEnding(room_out.get(), dataver);
    room_out.reset();

    // If we saved the new room into the memory, now it's the time to overwrite
    // the original room with the accumulated data
    if (!out_roomfile)
    {
        std::unique_ptr<Stream> temp_room(new MemoryStream(temp_data));
        room_out.reset(File::CreateFile(in_roomfile));
        if (!room_out)
        {
            printf("Error: failed to open room file for writing.\n");
            return -1;
        }
        CopyStream(temp_room.get(), room_out.get(), temp_data.size());
    }
    printf("Done.\n");
    return 0;
}
