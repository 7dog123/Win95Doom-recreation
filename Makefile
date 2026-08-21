################################################################
#
# Win95Doom-remake - DOOM95 recreation build
# Cross compile with the mingw 32 bit toolchain:
#   make -f Makefile.w95
#
################################################################

CROSS	= i686-w64-mingw32-
CC	= $(CROSS)gcc
CXX	= $(CROSS)g++

CFLAGS	= -std=gnu89 -g -O1 -Wall -DWIN95
CXXFLAGS= -g -O1 -Wall -DWIN95
LDFLAGS	= -mwindows -static-libgcc
LIBS	= -lddraw -ldsound -lwinmm -lgdi32 -luser32 -ladvapi32 -lm

# subdirectory for objects
O	= obj

OBJS	=			\
	$(O)/doom95.o		\
	$(O)/i_w95gdk.o		\
	$(O)/i_main.o		\
	$(O)/i_vkey.o		\
	$(O)/i_sound.o		\
	$(O)/i_winmus.o		\
	$(O)/doomdef.o		\
	$(O)/doomstat.o		\
	$(O)/dstrings.o		\
	$(O)/tables.o		\
	$(O)/f_finale.o		\
	$(O)/f_wipe.o 		\
	$(O)/d_main.o		\
	$(O)/d_net.o		\
	$(O)/d_items.o		\
	$(O)/g_game.o		\
	$(O)/m_menu.o		\
	$(O)/m_misc.o		\
	$(O)/m_argv.o  		\
	$(O)/m_bbox.o		\
	$(O)/m_fixed.o		\
	$(O)/m_swap.o		\
	$(O)/m_cheat.o		\
	$(O)/m_random.o		\
	$(O)/am_map.o		\
	$(O)/p_ceilng.o		\
	$(O)/p_doors.o		\
	$(O)/p_enemy.o		\
	$(O)/p_floor.o		\
	$(O)/p_inter.o		\
	$(O)/p_lights.o		\
	$(O)/p_map.o		\
	$(O)/p_maputl.o		\
	$(O)/p_plats.o		\
	$(O)/p_pspr.o		\
	$(O)/p_setup.o		\
	$(O)/p_sight.o		\
	$(O)/p_spec.o		\
	$(O)/p_switch.o		\
	$(O)/p_mobj.o		\
	$(O)/p_telept.o		\
	$(O)/p_tick.o		\
	$(O)/p_saveg.o		\
	$(O)/p_user.o		\
	$(O)/r_bsp.o		\
	$(O)/r_data.o		\
	$(O)/r_draw.o		\
	$(O)/r_main.o		\
	$(O)/r_plane.o		\
	$(O)/r_segs.o		\
	$(O)/r_sky.o		\
	$(O)/r_things.o		\
	$(O)/w_wad.o		\
	$(O)/wi_stuff.o		\
	$(O)/v_video.o		\
	$(O)/st_lib.o		\
	$(O)/st_stuff.o		\
	$(O)/hu_stuff.o		\
	$(O)/hu_lib.o		\
	$(O)/s_sound.o		\
	$(O)/z_zone.o		\
	$(O)/info.o		\
	$(O)/sounds.o

all:	$(O) $(O)/DOOM95.EXE

$(O):
	mkdir -p $(O)

$(O)/DOOM95.EXE:	$(OBJS)
	$(CXX) $(LDFLAGS) $(OBJS) -o $@ $(LIBS)

$(O)/%.o:	%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(O)/%.o:	%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(O) $(O)/DOOM95.EXE

.PHONY: all clean
