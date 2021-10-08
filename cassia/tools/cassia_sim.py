import argparse
import ctypes
import numpy as np
from PIL import Image

parser = argparse.ArgumentParser(exit_on_error=False)
parser.add_argument('input', type=argparse.FileType('rb'))
args = parser.parse_args()

TILE_WIDTH_SHIFT = 3
TILE_HEIGHT_SHIFT = 3
TILE_WIDTH = 1<<TILE_WIDTH_SHIFT
TILE_HEIGHT = 1<<TILE_HEIGHT_SHIFT
COVER_SCALE = (1.0 / 16.0)
AREA_SCALE = (1.0 / 256.0)

class PSegment_bits(ctypes.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [
            ("cover",   ctypes.c_int64, 6),
            ("area",    ctypes.c_int64, 10),
            ("local_x", ctypes.c_uint64, TILE_WIDTH_SHIFT),
            ("local_y", ctypes.c_uint64, TILE_HEIGHT_SHIFT),
            ("layer",   ctypes.c_uint64, 16),
            ("tile_x",  ctypes.c_int64, 16 - TILE_WIDTH_SHIFT),
            ("tile_y",  ctypes.c_int64, 15 - TILE_HEIGHT_SHIFT),
            ("is_none", ctypes.c_uint64, 1),
        ]

class PSegment(ctypes.Union):
    _fields_ = [("b", PSegment_bits),
                ("asbyte", ctypes.c_uint64)]


a = np.fromfile(args.input, dtype=np.uint64)

# Shift X position to avoid negative X.
for i in range(len(a)):
    pseg = PSegment(asbyte=a[i])
    pseg.b.tile_x += 256
    a[i] = pseg.asbyte

segments = np.sort(a)

img = Image.new( 'RGB', (1000,1000), "black") # Create a new black image

class SimpleComputeSim:
    def __init__(self, local_dims):
        self.local_dims = local_dims

    def dispatch(self, dims):
        pending_invocations = []
        for (gz,gy,gx) in np.ndindex((dims[2], dims[1], dims[0])):
            sdata = self.create_shared_data()
            for (lz,ly,lx) in np.ndindex((self.local_dims[2],
                                          self.local_dims[1],
                                          self.local_dims[0])):
                        pending_invocations.append(
                            self.invocation_main(
                                sdata, (gx, gy, gz), (lx, ly, lz)))

        still_running = []
        while len(pending_invocations) > 0:
            for i in pending_invocations:
                try:
                    next(i)
                    still_running.append(i)
                except StopIteration:
                    pass
            pending_invocations = still_running
            still_running = []

    def create_shared_data(self):
        return None

    def invocation_main(self, sdata, global_pos, local_pos):
        pass


class TileRasterizerSim(SimpleComputeSim):
    WORKGROUP_WIDTH = 8
    WORKGROUP_HEIGHT = 1
    WORKGROUP_DEPTH = 1

    class SharedData:
        def __init__(self):
            # Tile local areas & covers
            self.areas = np.zeros((TILE_HEIGHT,TILE_WIDTH), dtype=np.int32)
            self.covers = np.zeros((TILE_HEIGHT,TILE_WIDTH+1), dtype=np.int32)

            # Index of 1st psegment for this tile
            self.group_index = 0

    def __init__(self):
        super(TileRasterizerSim, self).__init__(local_dims=(
            self.WORKGROUP_WIDTH, self.WORKGROUP_HEIGHT, self.WORKGROUP_DEPTH))

    def create_shared_data(self):
        return self.SharedData()

    def invocation_main(self, sdata, global_pos, local_pos):
        global img
        global segments
        pixels = img.load()

        tile_y = global_pos[0]
        local_y = local_pos[0]

        sdata.areas[local_y, :] = 0
        sdata.covers[local_y, :] = 0

        ###########################################################
        # Locate the start of tile row's psegments

        if local_y == 0: # Only the 1st thread needs to do this
            sdata.group_index = len(segments)
            low = 0
            high = len(segments) - 1

            while low <= high:
                mid = int((low + high) / 2)
                pseg = PSegment(asbyte=segments[mid])

                if (pseg.b.is_none or pseg.b.tile_y > tile_y):
                    high = mid - 1
                elif (pseg.b.tile_y < tile_y):
                    low = mid + 1
                else:
                    sdata.group_index = mid
                    high = mid - 1

        yield 1 # === Workgroup barrier ===

        # Invocations look at psegnets with their own offset
        curr_index = sdata.group_index + local_y

        ###########################################################
        # Now loop through the tiles
        for pos_x in range(0, img.size[0], TILE_WIDTH):
            pos_tile_x = (pos_x >> TILE_WIDTH_SHIFT) + 256

            ###########################################################
            # Cooperatively accumulate the areas & covers in the tile

            yield 1 # === Workgroup barrier ===

            # Loop through psegments in the current tile
            while curr_index < len(segments):
                pseg = PSegment(asbyte=segments[curr_index])

                # Stop when reaching end of tile's segments
                if pseg.b.is_none or \
                   pseg.b.tile_x > pos_tile_x or \
                   pseg.b.tile_y > tile_y:
                    break

                # Accumulate areas & covers
                # These would have to be atomic adds on the GPU.
                if pseg.b.tile_x == pos_tile_x:
                    sdata.areas[pseg.b.local_y, pseg.b.local_x] += pseg.b.area
                    sdata.covers[pseg.b.local_y, pseg.b.local_x + 1] += pseg.b.cover
                else:
                    sdata.covers[pseg.b.local_y, 0] += pseg.b.cover
                curr_index += self.WORKGROUP_WIDTH

            yield 1 # === Workgroup barrier ===

            ###########################################################
            # Output the tile
            cover = 0
            for loc_x in range(0, TILE_HEIGHT):
                area = sdata.areas[local_y, loc_x]
                sdata.areas[local_y, loc_x] = 0
                cover += sdata.covers[local_y, loc_x]
                sdata.covers[local_y, loc_x] = 0

                coverage = cover * COVER_SCALE + area * AREA_SCALE
                grey = int(max(0, min(1.0, coverage)) * 255)
                pixels[pos_x + loc_x, (tile_y<<3) + local_y] = (grey, grey, grey)

            # Save output covers for next tile
            sdata.covers[local_y, 0] = cover + sdata.covers[local_y, TILE_HEIGHT]
            sdata.covers[local_y, TILE_HEIGHT] = 0


tile_rasterizer = TileRasterizerSim()
tile_rasterizer.dispatch(dims=(int(img.size[1] / TILE_HEIGHT), 1, 1))

img.show()
