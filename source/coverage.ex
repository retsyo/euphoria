include std/filesys.e
include std/io.e

constant H_FILE = `
#ifndef COVERAGE_H_
#define COVERAGE_H_

#define COVER_LINE [1]cover_line
#define COVER_ROUTINE [1]cover_routine
#define WRITE_COVERAGE_DB [1]write_coverage_db

void COVER_LINE( int );
void COVER_ROUTINE( int );
long WRITE_COVERAGE_DB();

#endif
`

sequence cmd = command_line()
if length( cmd ) < 3 then
	puts(2, "Missing build directory\n")
	abort( 1 )
end if

procedure create_header( sequence builddir )
	sequence c_files = dir(builddir)
	integer found_file = 0, found_line = 0

	for i = 1 to length( c_files ) label "i_loop" do
		if eu:match( "coverage", c_files[i][D_NAME] ) then
			found_file = 1
		
			sequence lines = read_lines( builddir & '/' & c_files[i][D_NAME] )
			
			for j = 1 to length( lines ) do
				if match( "cover_line", lines[j] ) and match( "void _", lines[j] ) = 1 then
					found_line = 1
					integer matched = match( "cover_line", lines[j] )

					sequence m = lines[j][length("void _")..matched-1]
					atom out = open( builddir & "/back/coverage.h", "w", 1 )
					if out = -1 then
						printf(2, "Error opening target file: %s/back/coverage.h\n", { builddir }) 
						abort(1)
					end if
					--writefln( out, H_FILE, m[1][2] )
					writefln( out, H_FILE, m )
					printf(1, "done\n", {m})
					exit "i_loop"
				end if
			end for
			
		end if
	end for

	if not found_file then
		printf(2, "ERROR - no coverage.c file was located in %s\n", { builddir })
		abort(1)
	elsif not found_line then
		printf(2, "ERROR - cover_line() not found in coverage.c located at %s\n", { builddir })
		abort(1)
	end if
end procedure

for i = 3 to length( cmd ) do
	create_header( cmd[i] )
end for