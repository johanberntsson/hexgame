#!/usr/bin/env ruby

# diskutil.rb - a program for manipulating d81 disk images

# This program was written by Fredrik Ramsberg in 2026.
# It is provided for free. Since this program manipulates files, 
# backup the folder you're working in before using this program, 
# and don't run this program anywhere near important files.

# There is no malicious code in this program, and I've tried to
# be cautious (e.g. the program will refuse to overwrite any files, 
# or read or write files that start with '.' or contain '/' or '\').
# Still, use it at your own risk.

# This program uses a custom interleave scheme when writing files to
# for d81 disk images. If a disk image is then copied onto a real 
# floppy, and a program reads data from the floppy on a MEGA65, this 
# interleave scheme makes file reads about 5x faster, compared to the 
# default interleave scheme. To disable this scheme, add -defint to 
# command line.

# The interleave scheme is described here:
# https://files.mega65.org?ar=c134d07e-01fc-4d03-b584-e2369722d203

# I can be contacted at firstname dot lastname at the gmail dot com domain.

require 'fileutils'
require 'date'
require 'json'

$is_windows = (ENV['OS'] == 'Windows_NT')

$executables = nil

if $is_windows then
	$path_separator = "\\"
	$commandline_quotemark = "\""
else
	$path_separator = "/"
	$commandline_quotemark = "'"
end

$PRINT_DISK_MAP = false # Set to true to print which blocks are allocated

$filetypes = [
	"DEL", "SEQ", "PRG", "USR", "REL", "CBM",
	"???", "???", "???", "???", "???", "???", 
	"???", "???", "???", "???"
]

$d81interleave = [
	# 0:No interleave
	{},
	# 1: 0,1,  4,5,  8,9,  12,13, ...
	{	1 => 4, 5 => 8, 9 => 12, 13 => 16, 17 => 20, 21 => 24, 25 => 28, 29 => 32, 33 => 36, 37 => 0, 
		3 => 6, 7 => 10, 11 => 14, 15 => 18, 19 => 22, 23 => 26, 27 => 30, 31 => 34, 35 => 38, 39 => 2
	},
	# 2: 0, 4, 8, 12, ...
	{	0 => 4, 4 => 8, 8 => 12, 12 => 16, 16 => 20, 20 => 24, 24 => 28, 28 => 32, 32 => 36, 36 => 1,
		1 => 5, 5 => 9, 9 => 13, 13 => 17, 17 => 21, 21 => 25, 25 => 29, 29 => 33, 33 => 37, 37 => 2,
		2 => 6, 6 => 10, 10 => 14, 14 => 18, 18 => 22, 22 => 26, 26 => 30, 30 => 34, 34 => 38, 38 => 3,
		3 => 7, 7 => 11, 11 => 15, 15 => 19, 19 => 23, 23 => 27, 27 => 31, 31 => 35, 35 => 39
	},
]

$i81 = $d81interleave[1] # Optimal scheme for MEGA65, as far as we can tell.

class Disk_image
	def base_initialize
		@interleave = 1
		@skip_tracks = Array.new(@tracks)
		offset = 0
		@track_offset = @track_length.map {|len| k = offset; offset += len; k }
#		puts "offset = #{@track_offset}"
		@reserved_sectors = Array.new(@track_length.length, 0)
		@config_track_map = []
		@contents = Array.new(256 * @track_length.inject(0, :+), 0)
	end

	def calculate_initial_free_blocks
		@free_blocks = @track_length.inject(0, :+) - @reserved_sectors.inject(0, :+)
		puts "Free disk blocks at start: #{@free_blocks}" if $verbose
	end
	
	def free_blocks
		@free_blocks
	end

	def interleave
		@interleave
	end

	attr_accessor :interleave_scheme
	
	def save
#		add_directory()
		begin
			diskimage_file = File.open(@diskimage_filename, "wb")
		rescue
			puts "ERROR: Can't open #{@diskimage_filename} for writing"
			exit 1
		end
		diskimage_file.write @contents.pack("C*")
		diskimage_file.close
	end

	def get_track_length(track)
		@track_length[track]
	end

end  # class Disk_image

class D81_image < Disk_image
	
	def initialize(args)
		disk_title = args['disk_title']
		diskimage_filename = args['diskimage_filename']
		load = args['load']
		interleave = args['interleave']
		
		@interleave_scheme = interleave == 'mega65' ? $i81 : nil
		@tracks = 80
		@track_length = Array.new(@tracks + 1, 40)
		@track_length[0] = 0

		@diskimage_filename = diskimage_filename

		if load
			puts "Loading disk image #{diskimage_filename}..." if $verbose

			base_initialize() # Creates @contents, size based on contents of @track_length array

			@contents = File.binread(diskimage_filename).unpack("C*")

		else
			puts "Creating disk image #{diskimage_filename}..." if $verbose

			@disk_title = disk_title

			base_initialize() # Creates @contents, size based on contents of @track_length array

			calculate_initial_free_blocks()
				
			# BAM
			track4000 = [
				# $16500 = 91392 = 357 (18,0)
				0x28, 0x03, # track/sector of first directory sector
				0x44, # DOS version
				0x00, # Not used, don't alter value
				0x4F, 0x5A, 0x4D, 0x4F, 0x4F, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, # Disk name
				0xA0, 0xA0, # Not used, don't alter value 
				0x31, 0x41, # Disk ID
				0xA0, # Not used, don't alter value
				0x33, # DOS version
				0x44, # Disk version
				0xA0, 0xA0 # Not used, don't alter value
			]

			@contents[@track_offset[40] * 256 .. @track_offset[40] * 256 + track4000.length - 1] = track4000

			track4001 = [
				0x28,0x02, # track/sector of next BAM sector
				0x44, # Version#
				0xBB, # One's complement of version#
				0x31,0x41, # Disk ID (same as in 40:00)
				0xC0, # I/O byte
				0x00, # Auto-boot-loader flags
				0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, # Reserved for future use
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF, # Track 1
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF, # Track 2-4
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF, # Track 5-8
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF, # Track 9-12
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF, # Track 13-16
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF, # Track 17-20
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF, # Track 21-24
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF, # Track 25-28
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF, # Track 29-32
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF, # Track 33-36
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x24,0xF0,0xFF,0xFF,0xFF,0xFF, # Track 37-40
			# Sector 40:02
				0x00,0xFF, # track/sector of next BAM sector
				0x44, # Version#
				0xBB, # One's complement of version#
				0x31,0x41, # Disk ID (same as in 40:00)
				0xC0, # I/O byte
				0x00, # Auto-boot-loader flags
				0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, # Reserved for future use
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF, # Track 41
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF, # Track 42-44
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF, # Track 45-48
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF, # Track 49-52
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF, # Track 53-56
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF, # Track 57-60
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF, # Track 61-64
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF, # Track 65-68
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF, # Track 69-72
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF, # Track 73-76
				0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF,0x28,0xFF,0xFF,0xFF,0xFF,0xFF  # Track 77-80
			]

			# Add BAM at 40:01 and 40:02
			sector_start = (@track_offset[40] + 1) * 256
			@contents[sector_start, track4001.length] = track4001

			# Add a directory block
			@contents[(@track_offset[40] + 3) * 256, 256] = [0, 0xff] +
				Array.new(254, 0)

			set_title(disk_title)
		end
		
		# Create a disk image. Return number of free blocks, or -1 for failure.

#		puts "Free blocks: #{diskimage_filename}..." if $verbose

		@free_blocks
	end # initialize

	def set_title(ascii_title)
		name = ascii_to_16_char_petscii(ascii_title,0xA0).unpack("C*")
		title_start = @track_offset[40] * 256 + 4
		@contents[title_start, 16] = name #.pack("C*")
	end

	def set_id(ascii_id)
		id = ascii_to_petscii(ascii_id).unpack("C*")
		id_start = @track_offset[40] * 256 + 0x16
		@contents[id_start, 2] = id #.pack("C*")
	end

	def find_file(filename)
		# Returns [track, sector, offset, filetype] where the directory entry starts, or nil

		petscii_filename_arr = ascii_to_petscii(filename, 0xA0).unpack("C*")
		done = false
		track = 40
		sect = 3

		while !done
			sectpos = (@track_offset[track] + sect) * 256
			8.times do |entry|
				entrypos = sectpos + 32 * entry
				filetype = @contents[entrypos + 2]
				if filetype > 0
					if @contents[entrypos + 5, 16] == petscii_filename_arr
						subtype = filetype & 0x0f
						return [ track, sect, 32 * entry, $filetypes[subtype] ]
					end
				end
			end
			track = @contents[sectpos]
			sect = @contents[sectpos + 1]
			done = track == 0
		end

		nil		
	end

	def read_file(filename)
		file = find_file(filename)
		return nil unless file
		
		sectpos = (@track_offset[file[0]] + file[1]) * 256		
		track = @contents[sectpos + file[2] + 3]
		sect = @contents[sectpos + file[2] + 4]
		contents = []
		
		if track < 0 or track > 80 or sect < 0 or sect > 39
			puts "ERROR: Incorrect directory entry for #{filename}"
			return nil
		end
		
		while true
			sectpos = (@track_offset[track] + sect) * 256
			track = @contents[sectpos]
			sect = @contents[sectpos + 1]
			if track == 0
				contents += @contents[sectpos + 2, sect - 1]
				return {
					'contents' => contents,
					'type' => file[3]
				}
			else
				contents += @contents[sectpos + 2, 254]
			end
		end
		
	end

	def delete_file(filename)
		# Return either
		# If successful: nil 
		# If not successful: "Error message..."

		file = find_file(filename)
		return "File not found" unless file

		dir_track = file[0]
		dir_sect = file[1]
		dir_offset = file[2]
		
		dir_sectpos = (@track_offset[dir_track] + dir_sect) * 256		
		track = @contents[dir_sectpos + dir_offset + 3]
		sect = @contents[dir_sectpos + dir_offset + 4]
		
		if track < 0 or track > 80 or sect < 0 or sect > 39
			puts "ERROR: Incorrect directory entry for #{filename}"
			return nil
		end
		
		while true
			deallocate_sector(track, sect)
			sectpos = (@track_offset[track] + sect) * 256
			track = @contents[sectpos]
			sect = @contents[sectpos + 1]
			break if track == 0
		end
		
		@contents[dir_sectpos + dir_offset + 2] = 0x00 # File type = DEL
		
		nil
	end

	def add_file(filename, filecontents, filetype = 'SEQ') # Returns last sector used
		last_sector = nil
		sector_count = 0
		last_sector_used = nil
		if filecontents == nil or filecontents.length == 0
			start_sector = [0,0]
			last_sector_used = start_sector
		else
			start_sector = find_free_file_start_sector(last_sector)
			last_sector_used = start_sector
			if start_sector == nil
				puts "ERROR: No free blocks left on disk."
				exit 1
			end
			sector_count += 1
			this_sector = start_sector
			while filecontents.length > 254
				next_sector = find_next_free_sector(this_sector[0], this_sector[1])
				if next_sector == nil
					puts "ERROR: No free blocks left on disk."
					exit 1
				end
				last_sector_used = next_sector
				
				sector_count += 1
				block_contents = next_sector.pack("CC") + filecontents[0 .. 253]
				filecontents = filecontents[254 .. filecontents.size - 1]
				@contents[256 * (@track_offset[this_sector[0]] + this_sector[1]) .. 
							256 * (@track_offset[this_sector[0]] + this_sector[1]) + 255] =
					block_contents.unpack("C*")
				this_sector = next_sector
			end
			block_contents = [0, filecontents.length + 2 - 1].pack("CC") + filecontents + Array.new(254 - filecontents.length).fill(0).pack("c*")
			@contents[256 * (@track_offset[this_sector[0]] + this_sector[1]) .. 
						256 * (@track_offset[this_sector[0]] + this_sector[1]) + 255] =
				block_contents.unpack("C*")
		end
		
		# Add file to directory

		filetype_number = 0x81
		if filetype =~ /PRG/i
			filetype_number = 0x82;
		end

		dir_entry = [filetype_number] + start_sector +
			ascii_to_16_char_petscii(filename, 0xA0).unpack("C*") +
			Array.new(9, 0) + 
			[sector_count % 256, sector_count / 256]

		done = false
		track = 40
		sect = 3
		while !done
			sectpos = (@track_offset[track] + sect) * 256
			8.times do |entry|
				entrypos =  sectpos + 32 * entry
				filetype = @contents[entrypos + 2]
				if filetype == 0 # No file
					@contents[entrypos + 2, 30] = dir_entry
					done = true
					break
				end
			end
			unless done
				next_track = @contents[sectpos]
				next_sect = @contents[sectpos + 1]
				if next_track == 0
					# Allocate a new directory block
					next_track = 40
					next_sect = sect + 1
					while sector_allocated?(next_track, next_sect)
						next_sect += 1
						if next_sect > 39
							next_sect = 3
							while sector_allocated?(next_track, next_sect)
								next_sect += 1
								if next_sect >= sect
									raise "Directory full"
								end
							end
						end
					end
					@contents[sectpos, 2] = [40, next_sect]
					allocate_sector(40, next_sect)
					@contents[(@track_offset[40] + next_sect) * 256, 256] = [0, 0xff] +
						Array.new(254, 0)
#					puts "Wrote new directory block 40, #{next_sect}"
				end
				track = next_track
				sect = next_sect
			end
		end

		puts "Added file #{filename} to disk." if $verbose
		
		return last_sector_used
	end

	def directory
		files = []
		done = false
		track = 40
		sect = 3

		sectpos = @track_offset[track] * 256
		files += [{
			'header' => "\"" + petscii_to_ascii(@contents[sectpos + 4, 16]).ljust(16) +
					"\" " + petscii_to_ascii(@contents[sectpos + 0x16, 2]).ljust(3) +
					petscii_to_ascii(@contents[sectpos + 0x19, 2]),
			'size' => 0
		}]

		while !done
			sectpos = (@track_offset[track] + sect) * 256
			8.times do |entry|
#				puts "Examining entry #{entry}, track #{track}, sector #{sect}"
				entrypos =  sectpos + 32 * entry
				filetype = @contents[entrypos + 2]
#				puts "Filetype #{filetype}"
				if filetype != 0
					subtype = filetype & 0x0f
					locked = filetype & 64 > 0 ? "<" : " "
					closed = filetype & 128 > 0 ? " " : "*"
					files += [{ 
						'name' => petscii_to_ascii(@contents[entrypos + 5, 16]), 
						'type' => closed + $filetypes[subtype] + locked,
						'size' => @contents[entrypos + 30] + 256 * @contents[entrypos + 31]
					}]
				end
			end
			track = @contents[sectpos]
			sect = @contents[sectpos + 1]
			done = track == 0
		end

		sectpos = (@track_offset[40] + 1) * 256 + 16
		free = 0
		40.times do |i|
			free += @contents[sectpos + 6 * i] if i != 39
			free += @contents[sectpos + 256 + 6 * i]
		end
		
		files += [{
			'footer' => "blocks free",
			'size' => free
		}]


		files
	end

	private
	
	def allocate_sector(track, sector)
		print "*" if $PRINT_DISK_MAP
		index1 = (track > 40 ? 0x100: 0) + 0x10 +  6 * ((track - 1) % 40)
		index2 = index1 + 1 + (sector / 8)

		# adjust number of free sectors

		free = @contents[(@track_offset[40] + 1) * 256 + index1]
		if free < 1 or free > 40
			puts "BAD FREE TRACK SPACE: #{track}, #{sector}"
		end
		
		@contents[(@track_offset[40] + 1) * 256 + index1] = free - 1
		# allocate sector
		index3 = 255 - 2**(sector % 8)
		@contents[(@track_offset[40] + 1) * 256 + index2] &= index3
	end

	def deallocate_sector(track, sector)
#		print "*" if $PRINT_DISK_MAP
		index1 = (track > 40 ? 0x100: 0) + 0x10 +  6 * ((track - 1) % 40)
		index2 = index1 + 1 + (sector / 8)

		# adjust number of free sectors

		free = @contents[(@track_offset[40] + 1) * 256 + index1]
		if free > 39
			puts "BAD FREE TRACK SPACE: #{track}, #{sector}"
		end
		
		@contents[(@track_offset[40] + 1) * 256 + index1] = free + 1
		# deallocate sector
		index3 = 2**(sector % 8)
		@contents[(@track_offset[40] + 1) * 256 + index2] |= index3
	end

	def sector_allocated?(track, sector)
		index1 = (track > 40 ? 0x100: 0) + 0x10 +  6 * ((track - 1) % 40)
		index2 = index1 + 1 + (sector / 8)
		index3 = 2**(sector % 8)

		# is sector allocated?
		return @contents[(@track_offset[40] + 1) * 256 + index2] & index3 == 0
	end
	
	def find_free_file_start_sector(last_sector = nil)
		if last_sector
			return find_next_free_sector(last_sector[0], last_sector[1])
		else
			1.upto 40 do |t|
				40.times do |s|
					unless t > 39 or sector_allocated?(40 - t, s)
						allocate_sector(40 - t, s)
						return [40 - t, s]
					end
					unless sector_allocated?(40 + t, s)
						allocate_sector(40 + t, s)
						return [40 + t, s]
					end
				end
			end
		end
		return nil
	end

	def find_next_free_sector(track, sector)
		start_track = track
		tried_track_1 = nil
		tried_track_80 = nil
		tried_sectors = []
		if interleave_scheme && interleave_scheme.has_key?(sector)
			next_sector = interleave_scheme[sector]
		else
			next_sector = (sector + 1) % 40
		end
		loop do
			unless sector_allocated?(track, next_sector)
				allocate_sector(track, next_sector)
				return [track, next_sector]
			end
			tried_sectors.push next_sector unless tried_sectors.include? next_sector 
			if tried_sectors.length > 39
				# This track is full, go to next
				tried_track_1 = true if track == 1
				tried_track_80 = true if track == 80
				return nil if tried_track_1 and tried_track_80 # No free sectors, fail!
				# Choose a track to try next
				if track < 40
					if track > 1
						track -= 1
					else
						track = 41
					end
				else
					if track < 80
						track += 1
					else
						track = 39
					end
				end
				next_sector = (sector + 8)
				next_sector = next_sector - (next_sector % 2)
				next_sector = next_sector % 40
			else
				next_sector = (next_sector > 38) ? 0 : next_sector + 1
			end
		end
	end

end # class D81_image

################################## END Disk image classes

def ascii_to_petscii(in_name, padding_code = nil)
	out = in_name.dup
	in_name.length.times do |charno|
		code = in_name[charno].ord
		code &= 0xdf if code >= 0x61 and code <= 0x7a
		out[charno] = code.chr
	end

	if padding_code
		c = padding_code.chr
		while out.length < 16
			out += c
		end
	end
	out #[0 .. 15]
end

def ascii_to_16_char_petscii(name, padding_code = nil)
	out = ascii_to_petscii(name)
	if padding_code
		c = padding_code.chr
		while out.length < 16
			out += c
		end
	end
	out[0 .. 15]
end

def petscii_to_ascii(name)
	str = ""
	name.length.times do |charno|
		code = name[charno].ord
		code |= 0x20 if code >= 0x41 and code <= 0x5a
		str += code.chr
	end
	while str[-1] == 0xa0.chr or str[-1] == 0x00.chr
		str = str[0, str.length - 1]
	end
	str
end
	
def print_usage_and_exit
	print_usage
	exit 1
end

def matchFilePattern(filename:, name: nil, pattern: nil)
	if pattern
		return true if pattern.match? filename
	elsif filename == name
		return true
	end
	nil
end

def print_usage
	puts "Usage: diskutil.rb <disk_image_1> [disk_image_2]"
	puts "    [-list[1|2]]                              # List directory"
	puts "    [-name[1|2] <name>]                       # Set disk name"
	puts "    [-id[1|2] <id>]                           # Set disk ID"
	puts "    [-out <folder_name>]                      # Specify an output folder"
	puts "    [-minusok]                                # Allow patterns/names starting with '-'"
	puts "    [-defint]                                 # Use default interleave for d81"
	puts "    [-write<prg|seq|default>]                 # Set output file type"
	puts "    [-copy<f|1|2><f|1|2>[+hhhh|-] <pattern>*] # Copy file(s)"
	puts "    [-del[1|2] <pattern>]                     # Delete file(s)"
	puts "  disk_image_1: New or existing file, e.g. disk.d81"
	puts "  disk_image_2: New or existing file, e.g. disk2.d81 (optional)"
	puts "\nNOTE:" 
	puts "  * Patterns for matching files in disk images must be given in single"
	puts "    quotes, if they contain * or ?"
	puts "  * A -write command is active until the next -write command"
	puts "  * The output folder (if any) is created first, regardless where in"
	puts "    the command line it is specified. No more than one output folder"
	puts "    can be given."
	puts "  * Unless you use the -defint command, an interleave scheme optimized"
	puts"     for MEGA65 is used for d81 disk images."
	puts "\nSample commands:"
	puts "    -list                   List all files on disk image 1"
	puts "    -name \"my disk\"         Set the disk name of disk image 1"
	puts "    -id2 AB                 Set the disk ID of disk image 2"
	puts "    -copyf1 *.txt readme    Copy from file system to disk image 1"
	puts "    -copy1f- 'intro*'       Copy from disk image 1 to file system, removing the load address"
	puts "    -copyf2+0801 files#{$path_separator}*    Copy from file system to disk image 2, adding a load address"
	puts "    -del2 <pattern>         Delete from disk image 2"
end

if ARGV.length == 0
	print_usage_and_exit
end	

await_arg_count = 0
$verbose = false
$minusok = false
$defint = false
$write_format = 'default'
$output_folder = nil
$disk_images = []
i = 0

$command_queue = []
$command = nil
$arg_collection = []

# Read arguments

begin
	ARGV.each do |arg|
		i += 1
		if i == 1
			if arg =~ /^-/ or arg !~ /\.d(64|71|81)$/
				raise "Illegal filename for disk image 1: #{arg}" +
					"\nSample legal name: mydisk.d81"
			end
			$disk_images += [{'filename' => arg }]
#			puts "Pushed to #{$disk_images.to_s}!"
		elsif i == 2 and arg !~ /^-/ and arg =~ /\.d(64|71|81)$/
			$disk_images += [{'filename' => arg }]
		elsif arg =~ /^-defint$/ then
			$defint = true
			await_arg_count = 0
		elsif arg =~ /^-minusok$/ then
			$minusok = true
			await_arg_count = 0
		elsif arg =~ /^-write(prg|seq|default)$/ then
			$command = { 'cmd' => 'write_format' }
			await_arg_count = 0
			$command['arg'] = $arg_collection.length
			$arg_collection.push [$1]
			$command_queue.push $command
		elsif arg =~ /^-list([12])?$/ then
			$command = { 'cmd' => 'list' }
			$command['src'] = $1 == '2' ? '2' : '1'
			if $disk_images.length < 2 and $command['src'] == '2'
				raise "Command #{arg} refers to disk image 2, which has not been specified"
			end
			await_arg_count = 0
			$command_queue.push $command
#		elsif arg =~ /^-(copy|move)([f12])([f12])(\-|\+[0-9a-fA-F]{4})?$/ then
		elsif arg =~ /^-(copy)([f12])([f12])(\-|\+[0-9a-fA-F]{4})?$/ then
			$command = { 'cmd' => $1 }
			$command['src'] = $2
			$command['dest'] = $3
			$command['load'] = $4
			if $disk_images.length < 2 and $command['src'] == '2' || $command['dest'] == '2'
				raise "Command #{arg} refers to disk image 2, which has not been specified"
			end
			if $2 == $3
				raise "Command #{arg}: source can't be the same as destination"
			end
			await_arg_count = 10000
			$command['arg'] = $arg_collection.length
			$arg_collection.push []
			$command_queue.push $command
		elsif arg =~ /^-del([12])?$/ then
			$command = { 'cmd' => 'del' }
			$command['src'] = $1 == '2' ? '2' : '1'
			if $disk_images.length < 2 and $command['src'] == '2'
				raise "Command #{arg} refers to disk image 2, which has not been specified"
			end
			await_arg_count = 10000
			$command['arg'] = $arg_collection.length
			$arg_collection.push []
			$command_queue.push $command
		elsif arg =~ /^-name([12])?$/ then
			$command = { 'cmd' => 'name' }
			$command['src'] = $1 == '2' ? '2' : '1'
			if $disk_images.length < 2 and $command['src'] == '2'
				raise "Command #{arg} refers to disk image 2, which has not been specified"
			end
			await_arg_count = 1
			$command['arg'] = $arg_collection.length
			$arg_collection.push []
			$command_queue.push $command
		elsif arg =~ /^-out$/ then
			$command = { 'cmd' => 'output_folder' }
			await_arg_count = 1
			$command['arg'] = $arg_collection.length
			$arg_collection.push []
			$command_queue.push $command
		elsif arg =~ /^-id([12])?$/ then
			$command = { 'cmd' => 'id' }
			$command['src'] = $1 == '2' ? '2' : '1'
			if $disk_images.length < 2 and $command['src'] == '2'
				raise "Command #{arg} refers to disk image 2, which has not been specified"
			end
			await_arg_count = 1
			$command['arg'] = $arg_collection.length
			$arg_collection.push []
			$command_queue.push $command
		elsif await_arg_count > 0 then
			await_arg_count -= 1
			$arg_collection[-1].push arg
		else
			raise "Unknown or misformatted option: " + arg
		end
	end
	raise "No disk image file given" unless $disk_images.length > 0
rescue => e
	puts "ERROR: #{e.message}"
	puts "(For instructions, run program without arguments)"
	exit 1
end

#puts $command_queue.to_s
#puts $arg_collection.to_s

# Verify arguments AND create output folder, if needed

begin
	$command_queue.each do |command|
		cmd = command['cmd']
#		print "CMD " + cmd
		src = command['src']
		dest = command['dest']
#		print "DoneCMD "
#		print "DoneCMD " + command['arg']
		arg = command['arg'] ? $arg_collection[command['arg']] : nil
#		print "DoneCMD " + cmd
		load = command['load']
		if cmd == 'name'
			raise "No name given" if arg.length < 1
			raise "Disk name #{arg[0]} starts with '-', not allowed " +
					"without -minusok" if !$minusok and arg[0] =~ /^-/
		elsif cmd == 'output_folder'
			raise "Can't assign multiple output folders" if $output_folder
			raise "No output folder given" if arg.length < 1
			$output_folder = arg[0]
			raise "Output folder name #{$output_folder} starts with '-', not allowed " +
					"without -minusok" if !$minusok and $output_folder =~ /^-/
			if File.exist? $output_folder
				if !File.directory? $output_folder
					raise "Output folder name #{$output_folder} is not a folder"
				end
			else
				Dir.mkdir $output_folder
			end
		elsif cmd == 'id'
			raise "No ID given" if arg.length < 1
			raise "Disk ID must be two characters 0-9, A-Z" if arg[0] !~ /^[0-9a-z]{2}$/i
		elsif cmd == 'copy'
			raise "No files to copy given" if arg.length < 1
		elsif cmd == 'del'
			raise "No files to delete given" if arg.length < 1
		end
		
		if cmd == 'copy'
			if !$minusok
				arg.each do |filepath|
					raise "Filename #{filepath} starts with '-', not allowed " +
						"without -minusok" if filepath =~ /^-/
					raise "File #{filepath} not found." if src == 'f' and !File.exist?(filepath)
				end
			end
		elsif cmd == 'del'
			if !$minusok
				arg.each do |filepath|
					raise "Filename #{filepath} starts with '-', not allowed " +
						"without -minusok" if filepath =~ /^-/
				end
			end
		end
	end
rescue => e
	puts "ERROR: #{e.message}"
	puts "(For instructions, run program without arguments)"
	exit 1
#	print_usage_and_exit()
end

# Create missing disk images

$disk_images.each do |img|
	if File.exist? img['filename']
		img['image'] =
			D81_image.new( {
					'diskimage_filename' => img['filename'],
					'load' => true,
					'interleave' => ($defint ? 'default' : 'mega65')
					
			})
#		img['modified'] = true
	else
		if img['filename'] =~ /d81$/i
			img['image'] =
				D81_image.new( {
						'disk_title' => 'disk',
						'diskimage_filename' => img['filename'],
						'interleave' => ($defint ? 'default' : 'mega65')
				})
			img['modified'] = true
		end
	end
end

# Process all commands

$command_queue.each do |command|
	cmd = command['cmd']
	src = command['src']
	dest = command['dest']
	arg = command['arg'] ? $arg_collection[command['arg']] : nil
	load = command['load']
#	puts "CMD=#{cmd} arg=#{arg}"
	case cmd
		when 'output_folder' then
			nil
		when 'write_format' then
			$write_format = arg[0] # prg, seq or default
		when 'list' then
			img = $disk_images[src.to_i - 1]
			puts "\nDIRECTORY OF #{img['filename']}:"
			img['image'].directory.each do |file|
				if file['header']
					puts file['size'].to_s.ljust(2) + file['header']
				elsif file['footer']
					puts file['size'].to_s.ljust(5) + file['footer']
				else
					puts file['size'].to_s.ljust(5) + '"' + file['name'] + '"' +
						' ' * (16 - file['name'].length) + file['type']
				end
			end
		when 'name' then
			img = $disk_images[src.to_i - 1]
			img['image'].set_title(arg[0])
			img['modified'] = true
		when 'id' then
			img = $disk_images[src.to_i - 1]
			img['image'].set_id(arg[0].downcase)
			img['modified'] = true
		when 'copy' then
			puts "\nCOPY FROM " + 
				case src 
					when 'f' then 'file system'
					else $disk_images[src.to_i - 1]['filename']
				end +
				" to " +
				case dest 
					when 'f' then 'file system'
					else $disk_images[dest.to_i - 1]['filename']
				end
			if src == 'f'
				img = $disk_images[dest.to_i - 1]
				format = $write_format == 'seq' ? 'SEQ' : 'PRG'
				arg.each do |filepath|
					filename = filepath.split(/[\/\\]+/)[-1]
					next if filename =~ /^\./ # We don't read . files
					filename16 = filename[0 .. 15]
					data = File.binread(filepath)
					print "  COPYING " + filename.ljust(17) + 
							" (#{format}), " +
							"#{data.length.to_s.rjust(7)} bytes : "
					if load == '-'
						data = data[2 .. -1]
					elsif load =~ /\+(.{2})(.{2})/
						data = [$2.to_i(16), $1.to_i(16)].pack("CC") + data
					end
					if img['image'].find_file(filename16)
						print "ERROR: File exists in target file system!"
					else
						img['image'].add_file(
							filename16, 
							data, 
							format
						)
						img['modified'] = true
						print "OK"
					end
					puts ""
				end
			else
				# Source is a disk image
				srcimg = $disk_images[src.to_i - 1]
				destimg = $disk_images[dest.to_i - 1]
				dir = srcimg['image'].directory
#				puts "DIR is " + dir.to_s
				files = []
				arg.each do |filepattern|
					fname = filepattern
					regex = nil
					if filepattern =~ /[\*\?]/
						fname = nil
						str = '^' + 
							filepattern.gsub(".", "\\.").gsub("*", ".*").gsub("?",".") +
							'$'
#						puts "PATTERN " + str
						regex = Regexp.new str
					end
					dir.each do |dir_entry|
						dirfilename = dir_entry['name']
						next if dir_entry['type'] !~ / PRG| SEQ/
						if dirfilename and matchFilePattern(
									filename: dirfilename,
									name: fname,
									pattern: regex
								) and !files.include? dirfilename
							files.push dirfilename
#							puts "MATCH: " + dirfilename
						end
					end
				end
				files.each do |filename|
					file = srcimg['image'].read_file(filename)
					data = file['contents'].pack("C*")
					print "  COPYING " + filename.ljust(17) + 
							" (#{file['type']}), " +
							"#{data.length.to_s.rjust(7)} bytes : "
					if dest == 'f'
						final_filename = $output_folder ? 
								File.join($output_folder, filename) : filename
						if filename =~ /(^\.|\\|\/)/
							print "ERROR: Illegal filename characters"
						elsif File.exist? final_filename
							print "ERROR: File exists in target file system!"
						else
							if load == '-'
								data = data[2 .. -1]
							elsif load =~ /\+(.{2})(.{2})/
								data = [$2.to_i(16), $1.to_i(16)].pack("CC") + data
							end
							File.binwrite(final_filename, data)
							print "OK"
						end
					else
						# Target is a disk image
						if destimg['image'].find_file(filename)
							print "ERROR: File exists in target file system!"
						else
							f = srcimg['image'].find_file(filename)
							if !f
								# Should never occur!
								print "ERROR: File can't be found in source image"
							else
								format = $write_format == 'default' ? f[3] : $write_format.upcase
								
								destimg['image'].add_file(
									filename, 
									data,
									format
								)
								destimg['modified'] = true
								print "OK"
							end
						end
					end
					puts ""
				end
			end
		when 'del' then
			img = $disk_images[src.to_i - 1]
			dir = img['image'].directory
			puts "\nDELETE FILES FROM #{img['filename']}:"

			# Find all files to delete
			
			files = []
			arg.each do |filepattern|
				fname = filepattern
				regex = nil
				if filepattern =~ /[\*\?]/
					fname = nil
					str = '^' + 
						filepattern.gsub(".", "\\.").gsub("*", ".*").gsub("?",".") +
						'$'
#					puts "PATTERN " + str
					regex = Regexp.new str
				else
#					puts "NON-PATTERN " + fname
					nil
				end
				dir.each do |dir_entry|
					dirfilename = dir_entry['name']
					if dirfilename and matchFilePattern(
								filename: dirfilename,
								name: fname,
								pattern: regex
							) and !files.include? dirfilename
						files.push dirfilename
#						puts "MATCH: " + dirfilename
					end
				end
			end

			# Delete the files

			files.each do |filename|
				file = img['image'].read_file(filename)
				data = file['contents'].pack("C*")
				print "  DELETING " + filename.ljust(17) + 
						" (#{file['type']}), " +
						"#{data.length.to_s.rjust(7)} bytes : "
				result = img['image'].delete_file(filename)
				if result
					print result
				else
					print "OK"
				end
				img['modified'] = true
				puts ""
			end
		else
			raise "UNKNOWN COMMAND: #{cmd}"
	end
#	puts command.to_s
end

# Write any new or changed disk images

$disk_images.each do |img|
	if img['modified']
		img['image'].save()
	end
end

exit 0
