#include "SDL.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "time.h"
#include "math.h"

typedef double h_t;

/* Size of the grid, should be a power of 2 */
#define SIZE 256
#define ZOOM 2

/* mod x to SIZE, handling negative values correctly */
#define MOD(x) ((x+SIZE) % SIZE)
#define FOR_V(x,dx) int x = 0; x < SIZE; x += dx
#define FOR(x) FOR_V(x,1)

#define G_ALT 1.5
#define CLAMP 0.15
#define DAMP 0.995

#define TEMP_ALT 0.95
#define LAT_POLE 0.00
#define LAT_TILT 0.00
#define YEAR_LEN 1500

#define VAP_DIFF 6
#define WIND_SP 0.05

#define VAP_STD 0.005
#define VAP_TEMP 1.0
#define VAP_EXCHG 0.075

#define TIDE_AMP 0.01
#define TIDE_RATE 0.5

typedef h_t grid[SIZE][SIZE];

typedef struct {
  grid land;
  grid water;
  grid tide;
  grid xflow;
  grid yflow;
  grid temperature;
  grid latitude;
  grid vapor;
  grid rain;
  grid buffer;
} state_t;

long int t = 0;
int vx = 0;
int vy = 0;
state_t state;

h_t equilibrium_vapor(h_t temp) {
  return VAP_STD * SIZE * exp(VAP_TEMP * temp);
}

#define SEED_OCTAVES 4
#define BASE_ALT -0.10
#define SKEW 0.5
#define ALT_SCALE 1.0

void generate_land_point(grid g, int x, int y, int octave) {
  h_t skew = 0.0;

  octave = (octave <= SEED_OCTAVES) ? SEED_OCTAVES : octave;

  int du = SIZE >> ((octave + 1)/2);
  int dv = du * (octave % 2);
  h_t octave_scale = sqrt(du*du + dv*dv);
  h_t base = BASE_ALT * ALT_SCALE * octave_scale;

  if (octave > SEED_OCTAVES) {
    h_t a = g[MOD(x+du)][MOD(y+dv)];
    h_t b = g[MOD(x+du)][MOD(y-dv)];
    h_t c = g[MOD(x-du)][MOD(y-dv)];
    h_t d = g[MOD(x-dv)][MOD(y+du)];

    base = (a + b + c + d) / 4.0;
    skew = cbrt(
		pow(a - base, 3) +
		pow(b - base, 3) +
		pow(c - base, 3) +
		pow(d - base, 3)
		) / 4.0;
  }

  h_t noise = (h_t)rand()/(h_t)RAND_MAX;
  h_t disp = (noise - 0.5) * 2;
  g[x][y] = base + disp * octave_scale * ALT_SCALE + skew * SKEW;
}

void generate_land(grid g, long seed) {
  srand(seed);
  rand();

  generate_land_point(g, 0, 0, 0);

  for (int i = 0; (1 << i) < SIZE; i++) {
    int Dx = SIZE >> i;
    int dx = Dx/2;
    int octave = 2*i + 1;
    for (FOR_V(x,Dx)) {
      for (FOR_V(y,Dx)) {
	generate_land_point(g, x+dx, y+dx, octave); 
      }
    }
    octave += 1;
    for (FOR_V(x,Dx)) {
      for (FOR_V(y,Dx)) {
	generate_land_point(g, x+dx, y, octave); 
	generate_land_point(g, x, y+dx, octave); 
      }
    }
  }
}

void update_temperature() {
  for (FOR(x)) {
    for (FOR(y)) {
      state.temperature[x][y] = (
				 -(state.land[x][y]+state.water[x][y])*TEMP_ALT/SIZE +
				 -state.latitude[x][y]*state.latitude[x][y]*LAT_POLE +
				 state.latitude[x][y]*sin((double)t * M_PI * 2 / YEAR_LEN)*LAT_TILT
				 );
    }
  }  
}

void init_state(long seed) {
  generate_land(state.land, seed);
  for (FOR(x)) {
    for (FOR(y)) {
      state.water[x][y] = - state.land[x][y];
      state.water[x][y] *= state.water[x][y] > 0;
      state.latitude[x][y] = cos(x * M_PI * 2 / SIZE) + cos(y * M_PI * 2 / SIZE);
    }
  }
  update_temperature();
  for (FOR(x)) {
    for (FOR(y)) {
      state.vapor[x][y] = equilibrium_vapor(state.temperature[x][y]);
    }
  }
}

SDL_Window *sdl_win;
SDL_Renderer *sdl_ren;
SDL_Texture *sdl_tex;

unsigned char pixels[SIZE*ZOOM][SIZE*ZOOM][4];

void setup_sdl_stuff() {
  SDL_Init(SDL_INIT_VIDEO);

  sdl_win = SDL_CreateWindow("Watershed", 0, 0, SIZE*ZOOM, SIZE*ZOOM, 0);
  sdl_ren = SDL_CreateRenderer(sdl_win, -1, 0);
  sdl_tex = SDL_CreateTexture(sdl_ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STATIC, SIZE*ZOOM, SIZE*ZOOM);

  memset(pixels, 0, SIZE*ZOOM * SIZE*ZOOM * 4);
}

void teardown_sdl_stuff() {
  SDL_DestroyRenderer(sdl_ren);
  SDL_DestroyTexture(sdl_tex);
  SDL_DestroyWindow(sdl_win);
  SDL_Quit();
}

void flow_water() {
  for (FOR(x)) {
    for (FOR(y)) {
      state.tide[x][y] = TIDE_AMP*SIZE * sin((x + t*TIDE_RATE) * M_PI * 2 / SIZE);
    }
  }

  // update flow field
  for (FOR(x)) {
    for (FOR(y)) {

      for (int dx = 0; dx <= 1; dx++) {
	for (int dy = 0; dy <= 1; dy++) {
	  if (!dx != !dy) { //XOR
	    h_t dh =
	      (state.land[x][y]+state.tide[x][y]+state.water[x][y])-
	      (state.land[MOD(x+dx)][MOD(y+dy)]+state.tide[MOD(x+dx)][MOD(y+dy)]+state.water[MOD(x+dx)][MOD(y+dy)]);

	    int fromx = (dh > 0) ? x : MOD(x+dx);
	    int fromy = (dh > 0) ? y : MOD(y+dy);

	    h_t dP = (G_ALT / SIZE) * state.water[fromx][fromy];
	    dP = (dP < 0.475) ? dP : 0.475; // 0.5 is the threshold for stability

	    state.xflow[x][y] += dP * dh * dx;
	    state.yflow[x][y] += dP * dh * dy;
	  }
	}
      }
    }
  }

  // clamp flow
  for (FOR(x)) {
    for (FOR(y)) {
      h_t outflow = 0.0;

      for (int dx = -1; dx <= 1; dx++) {
	for (int dy = -1; dy <= 1; dy++) {
	  if (!dx != !dy) { //XOR
	    int flowx = (dx < 0) ? MOD(x-1) : x;
	    int flowy = (dy < 0) ? MOD(y-1) : y;
	    outflow += (state.xflow[flowx][flowy] * dx > 0) ? state.xflow[flowx][flowy] * dx : 0;
	    outflow += (state.yflow[flowx][flowy] * dy > 0) ? state.yflow[flowx][flowy] * dy : 0;
	  }
	}
      }

      if (outflow > 0) {
	h_t clamp = state.water[x][y] * CLAMP / outflow;
	clamp = (clamp < 1.0) ? clamp : 1.0;

	for (int dx = -1; dx <= 1; dx++) {
	  for (int dy = -1; dy <= 1; dy++) {
	    if (!dx != !dy) { //XOR
	      int flowx = (dx < 0) ? MOD(x-1) : x;
	      int flowy = (dy < 0) ? MOD(y-1) : y;
	      state.xflow[flowx][flowy] *= (state.xflow[flowx][flowy] * dx > 0) ? clamp : 1.0;
	      state.yflow[flowx][flowy] *= (state.yflow[flowx][flowy] * dy > 0) ? clamp : 1.0;
	    }
	  }
	}

      }
    }
  }

  // apply flow to calculate new water level
  for (FOR(x)) {
    for (FOR(y)) {
      state.xflow[x][y] *= DAMP;
      state.yflow[x][y] *= DAMP;

      state.water[x][y] -= (state.xflow[x][y] + state.yflow[x][y]);
      state.water[MOD(x+1)][MOD(y)] += state.xflow[x][y];
      state.water[MOD(x)][MOD(y+1)] += state.yflow[x][y];
    }
  }

}

void exchange_vapor() {
  for (FOR(x)) {
    for (FOR(y)) {
      h_t eq_vap = equilibrium_vapor(state.temperature[x][y]);
      h_t rain = VAP_EXCHG * (state.vapor[x][y] - eq_vap);
      rain = (rain > state.vapor[x][y]) ? state.vapor[x][y] : rain;
      rain = (rain < -state.water[x][y]) ? -state.water[x][y] : rain;
      state.water[x][y] += rain;
      state.vapor[x][y] -= rain;
      state.rain[x][y] = rain;
    }
  }  
}

void diffuse_vapor() {
  memcpy(state.buffer, state.vapor, sizeof(h_t)*SIZE*SIZE);

  for (FOR(x)) {
    for (FOR(y)) {

      state.vapor[x][y] = state.buffer[x][y] * (((h_t)VAP_DIFF - 4) / (h_t)VAP_DIFF);

      for (int dx = -1; dx <= 1; dx++) {
	for (int dy = -1; dy <= 1; dy++) {
	  if (!dx != !dy) { //XOR
	    state.vapor[x][y] += state.buffer[MOD(x+dx)][MOD(y+dy)] / VAP_DIFF;
	  }
	}
      }
    }
  }

  if (WIND_SP != 0) {
    int wdir = (WIND_SP > 0) - (WIND_SP < 0);

    memcpy(state.buffer, state.vapor, sizeof(h_t)*SIZE*SIZE);

    for (FOR(x)) {
      for (FOR(y)) {
	state.vapor[x][y] = WIND_SP * state.buffer[MOD(x+wdir)][y] + (1.0 - WIND_SP) * state.buffer[x][y];
      }
    }
  }
}

void update_state() {
  flow_water();
  update_temperature();
  exchange_vapor();
  diffuse_vapor();
  t += 1;
}

#define PAL_ALT 0
#define PAL_BIOME 1
#define PAL_FLOW 2
#define PAL_MOMENT 3

void render_state(int pal) {
  for (FOR(x)) {
    for (FOR(y)) {
      h_t water_alpha = exp(-state.water[x][y]*35);
      h_t greenery_alpha = exp(-state.water[x][y]*500);
      h_t scaled_alt = atan(state.land[x][y]*2*pow(2.0,SEED_OCTAVES*0.5) / SIZE - 1.0) / M_PI + 0.5;
      h_t light = atan(
		       (state.land[x][y]+state.water[x][y])-
		       (state.land[x][MOD(y-1)]+state.water[x][MOD(y-1)])
		       ) / M_PI * 0.9 + 0.5 + 0.1;
      h_t flow = atan((fabs(state.xflow[x][y]) + fabs(state.yflow[x][y])) * 35 / (state.water[x][y] + 0.00001)) / M_PI;
      h_t xmoment = atan(state.xflow[x][y] * 15 / (state.water[x][y] + 0.00001)) / M_PI;
      h_t ymoment = atan(state.yflow[x][y] * 15 / (state.water[x][y] + 0.00001)) / M_PI;

      unsigned char color[3];

      switch(pal) {

      case PAL_ALT:
	/* blue */  color[0] = (unsigned char)((1 - water_alpha)*light * 255);
	/* green */ color[1] = (unsigned char)((1 - scaled_alt)*water_alpha*light * 255);
	/* red */   color[2] = (unsigned char)(scaled_alt*water_alpha*light * 255);
	break;

      case PAL_BIOME:
	/* blue */  color[0] = (unsigned char)((1 - 0.9*water_alpha)*light * 255);
	/* green */ color[1] = (unsigned char)((1 - 0.8*greenery_alpha)*water_alpha*light * 255);
	/* red */   color[2] = (unsigned char)(0.45*greenery_alpha*water_alpha*light * 255);
	break;

      case PAL_FLOW:
	/* blue */  color[0] = (unsigned char)((1 - water_alpha + 0.2*light*water_alpha) * 255);
	/* green */ color[1] = (unsigned char)(((1 - water_alpha)*scaled_alt + 0.2*water_alpha*light) * 255);
	/* red */   color[2] = (unsigned char)(((1 - water_alpha)*flow + 0.2*light*water_alpha) * 255);
	break;

      case PAL_MOMENT:
	/* blue */  color[0] = (unsigned char)(((1 - water_alpha)*(ymoment + 0.5) + 0.2*light*water_alpha) * 255);
	/* green */ color[1] = (unsigned char)(((1 - water_alpha)*(xmoment*0.65 - ymoment*0.35 + 0.5) + 0.2*light*water_alpha) * 255);
	/* red */   color[2] = (unsigned char)(((1 - water_alpha)*(-xmoment*0.65 - ymoment*0.35 + 0.5) + 0.2*light*water_alpha) * 255);
	break;

      }

      for (int zx = 0; zx < ZOOM; zx++) {
	for (int zy = 0; zy < ZOOM; zy++) {
	  for (int rgb = 0; rgb < 3; rgb++) {
	    pixels[MOD(y-vy)*ZOOM+zy][MOD(x-vx)*ZOOM+zx][rgb] = color[rgb];
	  }
	}
      }
    }
  }
}

void render_to_screen() {
  SDL_UpdateTexture(sdl_tex, NULL, pixels, SIZE*ZOOM*4);
  SDL_RenderClear(sdl_ren);
  SDL_RenderCopy(sdl_ren, sdl_tex, NULL, NULL);
  SDL_RenderPresent(sdl_ren);
}

int main(int argc, char* argv[])
{
  long seed = time(NULL);
  int pause = 0;
  int quit = 0;
  int pal = PAL_ALT;

  if (argc > 1) { seed = atol(argv[1]); }
  printf("%lu\n", seed);
  init_state(seed);
  setup_sdl_stuff();

  clock_t ct = clock();

  SDL_Event e;
  while (!quit) {
    while (SDL_PollEvent(&e)){
      switch (e.type) {

      case SDL_QUIT:
	quit = 1;
	break;

      case SDL_KEYDOWN:
	switch (e.key.keysym.sym) {

	// main controls
	case SDLK_q:
	  quit = 1;
	  break;
	case SDLK_p:
	  pause = !pause;
	  break;

	// switch palette
	case SDLK_a:
	  pal = PAL_ALT;
	  break;
	case SDLK_s:
	  pal = PAL_BIOME;
	  break;
	case SDLK_d:
	  pal = PAL_FLOW;
	  break;
	case SDLK_f:
	  pal = PAL_MOMENT;
	  break;

	case SDLK_SPACE:
	  state.water[SIZE/2][SIZE/2] = SIZE;
	  break;

	// scroll
	case SDLK_UP:
	  vy = MOD(vy-1);
	  break;
	case SDLK_DOWN:
	  vy = MOD(vy+1);
	  break;
	case SDLK_LEFT:
	  vx = MOD(vx-1);
	  break;
	case SDLK_RIGHT:
	  vx = MOD(vx+1);
	  break;
	}
	break;
      default:
	break;
      }
    }

    if (pause) {
      SDL_Delay(150);
    }
    else {
      update_state();
    }

    if (t % 1 == 0) {
      render_state(pal);
      render_to_screen();
    }

    // printf("%lu\n", (clock() - ct) * 1000 / CLOCKS_PER_SEC);
    ct = clock();
  }

  teardown_sdl_stuff();

  return 0;
}
