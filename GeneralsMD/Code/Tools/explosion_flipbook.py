"""The two flipbooks the Reforged explosions play (ParticleSystemReforged.ini, FlipbookGrid): a
fireball that grows, rolls and cools over its 64 frames, and a dark smoke puff that turns over once
in 64 frames, so it loops without a seam.

Both are raymarched through a noise volume.  The fireball is a sphere whose surface is pushed out by
billowed noise (the sum of folded octaves, which gives round lobes with creases between them) that
rises and swells with the sphere; a temperature falls from the core outwards and with time, and runs
a ramp from yellow through saturated deep orange and red to soot.  It is alpha blended (stacked
additively it went to cream and white), so its colour is the ramp itself, never past it, and the late
frames turn to dark soot as the alpha thins.  The smoke is the same kind of sphere rotated rigidly
about a horizontal axis and lit from above by three density reads towards the light.  Both are DXT5
with straight alpha.

    python Tools/explosion_flipbook.py
        writes Data/Art/Textures/ReforgedExplosionFire.dds (8 by 8 frames of 256 pixels) and
        Data/Art/Textures/ReforgedExplosionSmoke.dds (8 by 8 frames of 128 pixels).
    python Tools/explosion_flipbook.py preview <folder>
        also writes both atlases as PNG there, the fireball on black and the smoke on grey.
    python Tools/explosion_flipbook.py selfcheck

Same bytes on every run: the noise comes from a seeded generator and nothing else varies.  Nothing is
drawn past REACH of a frame's half width, a margin of 13 pixels at 128, and every mip level has the
outermost texel ring of each frame cleared to nothing.  The chain runs two levels past 8-pixel frames
because DDSFileClass drops a file's two smallest levels, so the game's own chain ends there.  So neither
the bilinear filter nor a small mip pulls the neighbouring frame onto a quad's edge, which drew hard
lines round the smoke in game."""
import os
import struct
import sys

import numpy as np
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_FOLDER = os.path.join(HERE, "..", "Data", "Art", "Textures")
FIRE_NAME = "ReforgedExplosionFire.dds"
SMOKE_NAME = "ReforgedExplosionSmoke.dds"

GRID = 8						# frames a side: ParticleSystemReforged.ini's FlipbookGrid
FRAMES = GRID * GRID
FIRE_CELL = 256
SMOKE_CELL = 128
STEPS = 64					# samples along each ray through the unit cube
SEED = 1987
REACH = 0.80					# nothing is drawn past this radius, which keeps every frame's border empty
SMALLEST_CELL = 8				# the smallest mip the game loads has frames this many pixels a side
THROWAWAY_LEVELS = 2			# levels below it, which DDSFileClass drops on load (ddsfile.cpp)

NOISE_SIZE = 128				# the noise volume's side, periodic
NOISE_OCTAVES = (4, 8, 16, 32, 64)	# lattice cells a side for each octave
VOXELS_PER_UNIT = 30.0			# how much noise one unit of the cube spans

# The fireball.  Radii are in the cube's half width, so 1 touches the frame's edge.
FIRE_RADIUS_START = 0.25
FIRE_RADIUS_END = 0.46
FIRE_BILLOW = 0.70				# how far the billows push the surface, as a share of the radius
FIRE_LOBE_SCALE = 0.75			# below 1 makes the billows bigger
FIRE_CREASE = 0.9				# how much cooler the creases between billows burn than the lobes
FIRE_RISE = 0.9					# noise units the billows climb over the life
FIRE_SWIRL = 0.6				# radians the billows turn about the vertical over the life
FIRE_ABSORB = 16.0				# optical depth of one unit of full density
# Yellow, orange, saturated deep orange, red, soot: (temperature, linear colour).  No white: the
# hottest is a yellow, so the body never reads cream however it is lit.
FIRE_RAMP = (
	(0.00, (0.035, 0.025, 0.020)),
	(0.20, (0.22, 0.05, 0.015)),
	(0.40, (0.70, 0.17, 0.020)),
	(0.60, (0.98, 0.38, 0.035)),
	(0.80, (1.00, 0.60, 0.090)),
	(1.00, (1.00, 0.80, 0.280)),
)

SMOKE_RADIUS = 0.48
SMOKE_BILLOW = 0.65
SMOKE_SOFT = 0.35				# the depth over which the edge thickens to full, as a share of the radius
SMOKE_ABSORB = 14.0
SMOKE_DARK = 0.04				# linear grey on the shadowed side
SMOKE_LIT = 0.62				# linear grey on the side towards the light
SMOKE_LIGHT_STEP = 0.08			# spacing of the three reads towards the light
SMOKE_LIGHT_ABSORB = 1.6		# how much those reads darken the sample


def noise_volumes():
	"""Two periodic noise volumes, both NOISE_SIZE a side: billowed (the shape) and plain fBm (the
	temperature and the domain warp)."""
	rng = np.random.default_rng(SEED)
	billow = np.zeros((NOISE_SIZE,) * 3, np.float32)
	smooth = np.zeros((NOISE_SIZE,) * 3, np.float32)
	amplitude = 1.0
	total = 0.0
	for cells in NOISE_OCTAVES:
		for target, fold in ((billow, True), (smooth, False)):
			lattice = rng.random((cells,) * 3, dtype=np.float32)
			octave = ndimage.zoom(lattice, NOISE_SIZE / cells, order=3, mode="grid-wrap", grid_mode=True)
			target += amplitude * (np.abs(octave * 2.0 - 1.0) if fold else octave)
		total += amplitude
		amplitude *= 0.5
	return billow / total, smooth / total


def sample(volume, points):
	"""The volume at points (n, 3) in noise units, trilinear and wrapping."""
	coordinates = (points * VOXELS_PER_UNIT).T
	return ndimage.map_coordinates(volume, coordinates, order=1, mode="grid-wrap", prefilter=False)


def cube_points(cell):
	"""Every sample of an orthographic view down -z through the cube [-1, 1]^3: (cell, cell, STEPS, 3),
	row 0 at the top (y = 1), the first step nearest the eye (z = 1)."""
	axis = (np.arange(cell, dtype=np.float32) + 0.5) / cell * 2.0 - 1.0
	depth = 1.0 - (np.arange(STEPS, dtype=np.float32) + 0.5) / STEPS * 2.0
	y, x, z = np.meshgrid(-axis, axis, depth, indexing="ij")
	return np.stack((x, y, z), axis=-1)


def ramp(temperature):
	keys = np.array([key for key, _ in FIRE_RAMP], np.float32)
	colours = np.array([colour for _, colour in FIRE_RAMP], np.float32)
	return np.stack([np.interp(temperature, keys, colours[:, c]) for c in range(3)], axis=-1)


def composite(density, emission):
	"""Front to back along the steps: density (h, w, s) as optical depth a step, emission (h, w, s, 3).
	Returns the premultiplied colour and the alpha."""
	colour = np.zeros(density.shape[:2] + (3,), np.float32)
	transmit = np.ones(density.shape[:2], np.float32)
	for step in range(density.shape[2]):
		absorbed = 1.0 - np.exp(-density[:, :, step])
		colour += (transmit * absorbed)[..., None] * emission[:, :, step]
		transmit *= 1.0 - absorbed
	return colour, 1.0 - transmit


def fire_frame(index, points, radius_of_point, billow, smooth):
	t = index / (FRAMES - 1.0)
	radius = FIRE_RADIUS_START + (FIRE_RADIUS_END - FIRE_RADIUS_START) * (1.0 - (1.0 - t) ** 2.5)
	reach = min(radius * (1.0 + FIRE_BILLOW), REACH)
	inside = radius_of_point < reach
	p = points[inside]
	# The billows swell with the sphere, climb and turn slowly about the vertical.
	angle = FIRE_SWIRL * t
	cos, sin = np.cos(angle), np.sin(angle)
	q = p / np.float32(radius ** 0.6)
	q = np.stack((q[:, 0] * cos - q[:, 2] * sin, q[:, 1] - FIRE_RISE * t, q[:, 0] * sin + q[:, 2] * cos), axis=-1)
	warp = sample(smooth, q * 0.7 + 11.0) - 0.5
	q = q + warp[:, None] * 0.6
	shape = sample(billow, q * FIRE_LOBE_SCALE)
	r = radius_of_point[inside]
	surface = radius * (1.0 - FIRE_BILLOW * 0.55 + FIRE_BILLOW * shape * 1.6)
	# Late in the life the cloud thins from the inside out and the last frames empty.
	thin = np.clip((t - 0.55) / 0.45, 0.0, 1.0)
	solid = np.clip((surface - r) / (0.06 * radius), 0.0, 1.0) * (1.0 - thin * thin)
	solid *= np.clip(1.0 - thin * 1.2 + shape * 1.2 * thin, 0.0, 1.0)
	# Hot in the core and early; the creases between billows run cooler than the lobes.
	heat = 1.25 - 1.2 * t ** 0.8
	temperature = heat * (0.45 + 0.55 * np.clip(1.0 - r / surface, 0.0, 1.0) ** 0.5)
	temperature += (shape - 0.5) * FIRE_CREASE + (sample(smooth, q * 1.9 + 37.0) - 0.5) * 0.4
	temperature = np.clip(temperature, 0.0, 1.0)
	# The glow dies with the cloud: what is left late is soot.
	temperature *= (1.0 - thin) ** 1.5

	density = np.zeros(points.shape[:3], np.float32)
	emission = np.zeros(points.shape[:3] + (3,), np.float32)
	density[inside] = solid * (FIRE_ABSORB * 2.0 / STEPS)
	emission[inside] = ramp(temperature)
	colour, alpha = composite(density, emission)
	# Straight alpha, black where nothing is, so the filter darkens the edge a little and never
	# brightens it; then the gamma the game's textures are painted in.
	straight = np.where(alpha[..., None] > 1e-3, colour / np.maximum(alpha[..., None], 1e-3), 0.0)
	straight = np.clip(straight, 0.0, 1.0) ** (1.0 / 2.2)
	return np.concatenate((straight, alpha[..., None]), axis=-1)


def smoke_frame(index, points, radius_of_point, billow, smooth):
	angle = 2.0 * np.pi * index / FRAMES
	cos, sin = np.cos(angle), np.sin(angle)
	reach = min(SMOKE_RADIUS * (1.0 + SMOKE_BILLOW), REACH)
	inside = radius_of_point < reach
	p = points[inside]

	def solid_at(at):
		# Turned rigidly about the x axis: a whole turn in FRAMES, so frame FRAMES is frame 0.
		q = np.stack((at[:, 0], at[:, 1] * cos - at[:, 2] * sin, at[:, 1] * sin + at[:, 2] * cos), axis=-1)
		warp = sample(smooth, q * 0.7 + 23.0) - 0.5
		shape = sample(billow, q * 1.3 + warp[:, None] * 0.5 + 5.0)
		r = np.sqrt((at * at).sum(axis=-1))
		surface = SMOKE_RADIUS * (1.0 - SMOKE_BILLOW * 0.55 + SMOKE_BILLOW * shape * 1.6)
		return np.clip((surface - r) / (SMOKE_SOFT * SMOKE_RADIUS), 0.0, 1.0) ** 1.5

	solid = solid_at(p)
	# Lit from above and a little from the front: the smoke between the sample and the light shades
	# it, so the top of each billow is pale and the underside and the creases go dark.
	toward = np.array([0.25, 0.9, 0.35], np.float32)
	toward /= np.sqrt((toward * toward).sum())
	blocked = sum(solid_at(p + toward * (SMOKE_LIGHT_STEP * k)) for k in (1, 2, 3))
	light = np.exp(-SMOKE_LIGHT_ABSORB * blocked)
	grey = SMOKE_DARK + (SMOKE_LIT - SMOKE_DARK) * light

	density = np.zeros(points.shape[:3], np.float32)
	emission = np.zeros(points.shape[:3] + (3,), np.float32)
	density[inside] = solid * (SMOKE_ABSORB * 2.0 / STEPS)
	emission[inside] = np.repeat(grey[:, None], 3, axis=1)
	colour, alpha = composite(density, emission)
	# Straight alpha; where nothing is, the colour the edge would have, so the filter brings no fringe.
	straight = np.where(alpha[..., None] > 1e-3, colour / np.maximum(alpha[..., None], 1e-3), SMOKE_DARK)
	straight = np.clip(straight, 0.0, 1.0) ** (1.0 / 2.2)
	return np.concatenate((straight, alpha[..., None]), axis=-1)


def atlas(render, cell, channels, billow, smooth):
	points = cube_points(cell)
	radius_of_point = np.sqrt((points * points).sum(axis=-1))
	image = np.zeros((GRID * cell, GRID * cell, channels), np.float32)
	for index in range(FRAMES):
		row, column = divmod(index, GRID)
		image[row * cell:(row + 1) * cell, column * cell:(column + 1) * cell] = render(index, points, radius_of_point, billow, smooth)
	return image


# ---------------------------------------------------------------------------------------------
# DDS, DXT5.  Colour endpoints on each block's principal axis, inset by a sixteenth, indices to the
# nearest palette entry.  Plain, but enough for soft fire and smoke.

def to565(rgb):
	r = np.round(rgb[..., 0] * 31.0 / 255.0).astype(np.uint32)
	g = np.round(rgb[..., 1] * 63.0 / 255.0).astype(np.uint32)
	b = np.round(rgb[..., 2] * 31.0 / 255.0).astype(np.uint32)
	return (r << 11) | (g << 5) | b


def from565(c):
	r = (c >> 11) & 31
	g = (c >> 5) & 63
	b = c & 31
	return np.stack(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)), axis=-1).astype(np.float32)


def blocks(image):
	h, w, c = image.shape
	return image.reshape(h // 4, 4, w // 4, 4, c).transpose(0, 2, 1, 3, 4).reshape(-1, 16, c)


def colour_blocks(rgb):
	"""rgb (n, 16, 3) in 0..255: n DXT colour blocks, always in four colour mode."""
	# Endpoints on the block's principal axis, which the box corners miss when the channels run
	# opposite ways.
	mean = rgb.mean(axis=1, keepdims=True)
	centred = rgb - mean
	axis = np.linalg.eigh(np.einsum("nki,nkj->nij", centred, centred))[1][:, :, 2]
	along = np.einsum("nki,ni->nk", centred, axis)
	low = np.clip(mean[:, 0] + axis * along.min(axis=1, keepdims=True), 0.0, 255.0)
	high = np.clip(mean[:, 0] + axis * along.max(axis=1, keepdims=True), 0.0, 255.0)
	inset = (high - low) / 16.0
	c0 = to565(high - inset)
	c1 = to565(low + inset)
	swap = c0 < c1
	c0, c1 = np.where(swap, c1, c0), np.where(swap, c0, c1)
	p0 = from565(c0)
	p1 = from565(c1)
	palette = np.stack((p0, p1, (2.0 * p0 + p1) / 3.0, (p0 + 2.0 * p1) / 3.0), axis=1)
	distance = ((rgb[:, :, None, :] - palette[:, None, :, :]) ** 2).sum(axis=-1)
	index = distance.argmin(axis=-1).astype(np.uint32)
	index[c0 == c1] = 0
	bits = (index << (2 * np.arange(16, dtype=np.uint32))).sum(axis=1, dtype=np.uint64).astype(np.uint32)
	out = np.zeros((rgb.shape[0], 8), np.uint8)
	out[:, 0:2] = c0.astype("<u2").view(np.uint8).reshape(-1, 2)
	out[:, 2:4] = c1.astype("<u2").view(np.uint8).reshape(-1, 2)
	out[:, 4:8] = bits.astype("<u4").view(np.uint8).reshape(-1, 4)
	return out


def alpha_blocks(alpha):
	"""alpha (n, 16) in 0..255: n DXT5 alpha blocks, in eight value mode."""
	a0 = alpha.max(axis=1).round().astype(np.uint32)
	a1 = alpha.min(axis=1).round().astype(np.uint32)
	weights = np.array([0, 7, 1, 2, 3, 4, 5, 6], np.float32)	# palette entry i is a0 + (a1 - a0) * weight / 7
	palette = a0[:, None] + (a1[:, None].astype(np.float32) - a0[:, None]) * weights / 7.0
	palette = np.floor(palette + 0.5)
	index = np.abs(alpha[:, :, None] - palette[:, None, :]).argmin(axis=-1).astype(np.uint64)
	index[a0 == a1] = 0
	bits = (index << (3 * np.arange(16, dtype=np.uint64))).sum(axis=1, dtype=np.uint64)
	out = np.zeros((alpha.shape[0], 8), np.uint8)
	out[:, 0] = a0
	out[:, 1] = a1
	out[:, 2:8] = bits.astype("<u8").view(np.uint8).reshape(-1, 8)[:, :6]
	return out


def clear_cell_rings(level, cell):
	"""Every frame's outermost ring of texels emptied, so a quad's edge samples nothing."""
	alpha = level[..., 3]
	for edge in range(0, level.shape[0], cell):
		alpha[edge, :] = alpha[edge + cell - 1, :] = 0.0
		alpha[:, edge] = alpha[:, edge + cell - 1] = 0.0
	return level


def mips(image, cell):
	"""The chain down to frames SMALLEST_CELL pixels a side and THROWAWAY_LEVELS more for the loader
	to drop, each level the box average of the one above with the colour weighted by its alpha, and
	every level's frame rings cleared."""
	levels = [clear_cell_rings(image.copy(), cell)]
	while cell > SMALLEST_CELL >> THROWAWAY_LEVELS:
		top = levels[-1]
		weighted = np.concatenate((top[..., :3] * top[..., 3:], top[..., 3:]), axis=-1)
		h, w, c = weighted.shape
		level = weighted.reshape(h // 2, 2, w // 2, 2, c).mean(axis=(1, 3))
		alpha = level[..., 3:]
		level = np.concatenate((np.where(alpha > 1e-4, level[..., :3] / np.maximum(alpha, 1e-4), 0.0), alpha), axis=-1)
		cell //= 2
		levels.append(clear_cell_rings(level, cell))
	return levels


def dds(image, cell):
	"""image (h, w, 4) in 0..1, straight alpha, frames cell pixels a side, as a DXT5 DDS file's bytes
	with its mips."""
	levels = mips(image, cell)
	payload = []
	for level in levels:
		if level.shape[0] < 4:	# a level under one block is stored as a whole block
			level = np.pad(level, ((0, 4 - level.shape[0]), (0, 4 - level.shape[1]), (0, 0)), mode="edge")
		texels = blocks(np.clip(np.round(level * 255.0), 0.0, 255.0))
		payload.append(np.concatenate((alpha_blocks(texels[:, :, 3]), colour_blocks(texels[:, :, :3])), axis=1).tobytes())
	h, w = image.shape[:2]
	flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000		# caps, height, width, pixel format, mip count, linear size
	header = b"DDS " + struct.pack("<7I", 124, flags, h, w, len(payload[0]), 0, len(levels)) + bytes(44)
	header += struct.pack("<2I4s5I", 32, 0x4, b"DXT5", 0, 0, 0, 0, 0)
	header += struct.pack("<5I", 0x8 | 0x1000 | 0x400000, 0, 0, 0, 0)	# complex, texture, mipmap
	assert len(header) == 128
	return header + b"".join(payload)


def decode_colour_block(block):
	c0, c1, bits = struct.unpack("<HHI", bytes(block))
	p0 = from565(np.uint32(c0))
	p1 = from565(np.uint32(c1))
	palette = (p0, p1, (2 * p0 + p1) / 3, (p0 + 2 * p1) / 3)
	return np.array([palette[(bits >> (2 * i)) & 3] for i in range(16)])


def selfcheck():
	# A block on one line between two colours, the case DXT is built for, comes back close.
	rng = np.random.default_rng(3)
	ends = rng.random((5, 2, 3)) * 255.0
	along = np.linspace(0.0, 1.0, 16)[None, :, None]
	rgb = ends[:, :1] * (1.0 - along) + ends[:, 1:] * along
	rgb[0] = 128.0		# a flat block takes index 0 throughout
	encoded = colour_blocks(rgb)
	for block, source in zip(encoded, rgb):
		assert np.abs(decode_colour_block(block) - source).max() < 32.0
	assert struct.unpack("<I", bytes(encoded[0][4:8]))[0] == 0
	alpha = np.tile(np.linspace(0.0, 255.0, 16), (2, 1))
	alpha[1] = 77.0
	coded = alpha_blocks(alpha)
	assert coded[0][0] == 255 and coded[0][1] == 0 and coded[1][0] == coded[1][1] == 77
	assert not coded[1][2:].any()
	tiny = dds(np.zeros((8, 8, 4), np.float32), 8)
	assert len(tiny) == 128 + 4 * 16 + 16 + 16 and tiny[:4] == b"DDS "
	# A solid atlas: the chain runs to 8-pixel frames plus two levels for the loader to drop, and
	# every level it keeps has its frames' rings empty and their insides full.
	levels = mips(np.ones((64, 64, 4), np.float32), 32)
	assert [level.shape[0] for level in levels] == [64, 32, 16, 8, 4]
	for level, cell in zip(levels[:-THROWAWAY_LEVELS], (32, 16, 8)):
		assert not level[::cell, :, 3].any() and not level[cell - 1::cell, :, 3].any()
		assert level[cell // 2, cell // 2, 3] > 0.0


def main():
	selfcheck()
	if sys.argv[1:2] == ["selfcheck"]:
		return
	billow, smooth = noise_volumes()
	fire = atlas(fire_frame, FIRE_CELL, 4, billow, smooth)
	smoke = atlas(smoke_frame, SMOKE_CELL, 4, billow, smooth)
	for name, image, cell in ((FIRE_NAME, fire, FIRE_CELL), (SMOKE_NAME, smoke, SMOKE_CELL)):
		path = os.path.normpath(os.path.join(OUT_FOLDER, name))
		with open(path, "wb") as out:
			out.write(dds(image, cell))
		print(path)
	if sys.argv[1:2] == ["preview"]:
		from PIL import Image
		folder = sys.argv[2]
		# Both over a desert sand, the ground they are mostly seen against.
		sand = np.array([0.62, 0.50, 0.33], np.float32)
		for name, image in (("fire.png", fire), ("smoke.png", smoke)):
			over = image[..., :3] * image[..., 3:] + sand * (1.0 - image[..., 3:])
			Image.fromarray(np.uint8(over * 255.0 + 0.5)).save(os.path.join(folder, name))


if __name__ == "__main__":
	main()
