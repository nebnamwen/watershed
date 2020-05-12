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

#define SEA_LEVEL 0.04

#define G_ALT 1.5

#define TEMP_ALT 0.95
#define LAT_POLE 0.00
#define LAT_TILT 0.00
#define YEAR_LEN 1500

#define VAP_DIFF 6
#define WIND_SP 0.015

#define VAP_STD 0.01
#define VAP_TEMP 1.0
#define VAP_EXCHG 0.075

#define TIDE_AMP 0.10
#define TIDE_RATE 0.15

/* mod x to SIZE, handling negative values correctly */
#define MOD(x) ((x+SIZE) % SIZE)

#define FOR(x,dx) int x = 0; x < SIZE; x += dx

typedef h_t grid[SIZE][SIZE];

typedef struct {
  grid land;
  grid water;
  grid tide;
  grid xflow;
  grid yflow;
  grid xmoment;
  grid ymoment;
  grid xmombuf;
  grid ymombuf;
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

void generate_land_point(grid g, int x, int y, int du, int dv) {
  h_t average = (
		 g[MOD(x+du)][MOD(y+dv)] +
		 g[MOD(x-du)][MOD(y-dv)] +
		 g[MOD(x+dv)][MOD(y-du)] +
		 g[MOD(x-dv)][MOD(y+du)]
		 ) / 4.0;
  h_t displacement = (h_t)rand()/(h_t)RAND_MAX - 0.5;
  g[x][y] = average + displacement * sqrt(du*du + dv*dv);
}

void generate_land(grid g) {
  srand(time(NULL));
  rand();

  for (int Dx = SIZE; Dx > 1; Dx /= 2) {
    int dx = Dx/2;
    for (FOR(x,Dx)) {
      for (FOR(y,Dx)) {
	generate_land_point(g, x+dx, y+dx, dx, dx); 
      }
    }
    for (FOR(x,Dx)) {
      for (FOR(y,Dx)) {
	generate_land_point(g, x+dx, y, dx, 0); 
	generate_land_point(g, x, y+dx, dx, 0); 
      }
    }
  }
}

void update_temperature() {
  for (FOR(x,1)) {
    for (FOR(y,1)) {
      state.temperature[x][y] = (
				 -(state.land[x][y]+state.water[x][y])*TEMP_ALT/SIZE +
				 -state.latitude[x][y]*state.latitude[x][y]*LAT_POLE +
				 state.latitude[x][y]*sin((double)t * M_PI * 2 / YEAR_LEN)*LAT_TILT
				 );
    }
  }  
}

void init_state() {
  generate_land(state.land);
  for (FOR(x,1)) {
    for (FOR(y,1)) {
      state.water[x][y] = SEA_LEVEL * SIZE - state.land[x][y];
      state.water[x][y] *= state.water[x][y] > 0;
      state.latitude[x][y] = cos(x * M_PI * 2 / SIZE) + cos(y * M_PI * 2 / SIZE);
    }
  }
  // state.water[SIZE/2][SIZE/2] = SIZE;
  update_temperature();
  for (FOR(x,1)) {
    for (FOR(y,1)) {
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
  memcpy(state.buffer, state.water, sizeof(h_t)*SIZE*SIZE);
  memset(state.xflow, 0, sizeof(h_t)*SIZE*SIZE);
  memset(state.yflow, 0, sizeof(h_t)*SIZE*SIZE);
  for (FOR(x,1)) {
    for (FOR(y,1)) {
      state.tide[x][y] = TIDE_AMP*SIZE * sin((x + t*TIDE_RATE) * M_PI * 2 / SIZE);
    }
  }

  for (FOR(x,1)) {
    for (FOR(y,1)) {

      for (int dx = -1; dx <= 1; dx++) {
	for (int dy = -1; dy <= 1; dy++) {
	  if (!dx != !dy) { //XOR
	    // calculate diffuse flow contribution
	    h_t f = (
		     (state.land[x][y]+state.tide[x][y]+state.buffer[x][y])-
		     (state.land[MOD(x+dx)][MOD(y+dy)]+state.tide[MOD(x+dx)][MOD(y+dy)]+state.buffer[MOD(x+dx)][MOD(y+dy)])
		     ) / 5;
	    // momentum contribution
	    if (state.xmoment[x][y] * dx > 0) { f += state.xmoment[x][y] * dx; }
	    if (state.ymoment[x][y] * dy > 0) { f += state.ymoment[x][y] * dy; }
	    if (f > 0) {
	      h_t mf = state.buffer[x][y] / 5;
	      f = f <= mf ? f : mf;

	      // state.water[x][y] -= f;
	      // state.water[MOD(x+dx)][MOD(y+dy)] += f;
	      state.xflow[MOD(x+(dx-1)/2)][y] += f * dx;
	      state.yflow[x][MOD(y+(dy-1)/2)] += f * dy;
	    }
	  }
	}
      }
    }
  }

  // apply flow to calculate new water level
  for (FOR(x,1)) {
    for (FOR(y,1)) {
      state.water[x][y] -= state.xflow[x][y] + state.yflow[x][y];
      state.water[MOD(x+1)][MOD(y)] += state.xflow[x][y];
      state.water[MOD(x)][MOD(y+1)] += state.yflow[x][y];
    }
  }

  // calculate momentum changes
  memcpy(state.xmombuf, state.xmoment, sizeof(h_t)*SIZE*SIZE);
  memcpy(state.ymombuf, state.ymoment, sizeof(h_t)*SIZE*SIZE);
  for (FOR(x,1)) {
    for (FOR(y,1)) {
      for (int du = -1; du <= 1; du += 2) {
	h_t drop;
	drop = state.xflow[x][y] * du;
	if (drop > 0) {
	  int fromx = du > 0 ? x : MOD(x+1);
	  int tox = du > 0 ? MOD(x+1) : x;

	  h_t drop_xmom = state.xmombuf[fromx][y] * drop/state.buffer[fromx][y];
	  h_t drop_ymom = state.ymombuf[fromx][y] * drop/state.buffer[fromx][y];

	  h_t fromh = state.land[fromx][y] + state.tide[fromx][y] + state.buffer[fromx][y];
	  h_t toh = state.land[tox][y] + state.tide[tox][y] + state.water[tox][y];
	  h_t dh = (toh - fromh) * G_ALT / SIZE;

	  h_t new_drop_xmom = drop_xmom - drop * dh * du;

	  if (x == 0 && y == 0) {
	    printf("%i: (%f) %f =(%f)=> %f\n", du, drop, drop_xmom, dh, new_drop_xmom);
	  }

	  state.xmoment[fromx][y] -= drop_xmom;
	  state.xmoment[tox][y] += new_drop_xmom;

	  state.ymoment[fromx][y] -= drop_ymom;
	  state.ymoment[tox][y] += drop_ymom;
	}
	drop = state.yflow[x][y] * du;
	if (drop > 0) {
	  int fromy = du > 0 ? y : MOD(y+1);
	  int toy = du > 0 ? MOD(y+1) : y;

	  h_t drop_xmom = state.xmombuf[x][fromy] * drop/state.buffer[x][fromy];
	  h_t drop_ymom = state.ymombuf[x][fromy] * drop/state.buffer[x][fromy];

	  h_t fromh = state.land[x][fromy] + state.tide[x][fromy] + state.buffer[x][fromy];
	  h_t toh = state.land[x][toy] + state.tide[x][toy] + state.water[x][toy];
	  h_t dh = (toh - fromh) * G_ALT / SIZE;

	  h_t new_drop_ymom = drop_ymom - drop * dh * du;

	  state.xmoment[x][fromy] -= drop_xmom;
	  state.xmoment[x][toy] += drop_xmom;

	  state.ymoment[x][fromy] -= drop_ymom;
	  state.ymoment[x][toy] += new_drop_ymom;
	}
      }
    }
  }
}

void exchange_vapor() {
  for (FOR(x,1)) {
    for (FOR(y,1)) {
      h_t eq_vap = equilibrium_vapor(state.temperature[x][y]);
      h_t rain = VAP_EXCHG * (state.vapor[x][y] - eq_vap);
      rain = (rain > state.vapor[x][y]) ? state.vapor[x][y] : rain;
      rain = (rain < -state.water[x][y]) ? -state.water[x][y] : rain;
      if (rain < 0) {
	h_t evap_ratio = (state.water[x][y] + rain) / state.water[x][y];
	state.xmoment[x][y] *= evap_ratio;
	state.ymoment[x][y] *= evap_ratio;
      }
      state.water[x][y] += rain;
      state.vapor[x][y] -= rain;
      state.rain[x][y] = rain;
    }
  }  
}

void diffuse_vapor() {
  memcpy(state.buffer, state.vapor, sizeof(h_t)*SIZE*SIZE);

  for (FOR(x,1)) {
    for (FOR(y,1)) {

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

    for (FOR(x,1)) {
      for (FOR(y,1)) {
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
  for (FOR(x,1)) {
    for (FOR(y,1)) {
      h_t water_scaled = state.water[x][y]/SIZE;
      h_t water_alpha = exp(-state.water[x][y]*35);
      h_t greenery_alpha = exp(-state.water[x][y]*250);
      h_t scaled_alt = atan(state.land[x][y]*2 / SIZE) / M_PI + 0.5;
      h_t light = atan(
			(state.land[x][y]+state.water[x][y])-
			(state.land[x][MOD(y-1)]+state.water[x][MOD(y-1)])
			) / M_PI * 0.9 + 0.5 + 0.1;
      h_t flow = atan((fabs(state.xflow[x][y]) + fabs(state.yflow[x][y])) * 35) / M_PI;
      h_t xmoment = atan(state.xmoment[x][y] * 50 / state.water[x][y]) / M_PI + 0.5;
      h_t ymoment = atan(state.ymoment[x][y] * 50 / state.water[x][y]) / M_PI + 0.5;
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
	/* green */ color[1] = (unsigned char)(0.2*water_alpha*light * 255);
	/* red */   color[2] = (unsigned char)(((1 - water_alpha)*flow + 0.2*light*water_alpha) * 255);
	break;

      case PAL_MOMENT:
	/* blue */  color[0] = (unsigned char)(((1 - water_alpha)*ymoment + 0.2*light*water_alpha) * 255);
	/* green */ color[1] = (unsigned char)(0.2*water_alpha*light * 255);
	/* red */   color[2] = (unsigned char)(((1 - water_alpha)*xmoment + 0.2*light*water_alpha) * 255);
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
  int pause = 0;
  int quit = 0;
  int pal = PAL_ALT;

  clock_t ct = clock();
  init_state();

  setup_sdl_stuff();

  SDL_Event e;
  while (!quit) {
    while (SDL_PollEvent(&e)){
      switch (e.type) {
      case SDL_QUIT:
	quit = 1;
	break;
      case SDL_KEYDOWN:
	switch (e.key.keysym.sym) {
	case SDLK_q:
	  quit = 1;
	  break;
	case SDLK_p:
	  pause = !pause;
	  break;
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

    render_state(pal);
    render_to_screen();

    // printf("%lu\n", (clock() - ct) * 1000 / CLOCKS_PER_SEC);
    ct = clock();
  }

  teardown_sdl_stuff();

  return 0;
}
