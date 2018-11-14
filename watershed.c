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

#define RAIN 0.0002
#define DEPTH_SCALE 0.028
const h_t DEPTH = DEPTH_SCALE * SIZE;
/* DEPTH * DECAY + RAIN == DEPTH
   DECAY + RAIN/DEPTH = 1
   DECAY = 1 - RAIN/DEPTH */
const h_t DECAY = 1.0 - RAIN/DEPTH;

#define TIDE_AMP 0.05*SIZE
#define TIDE_RATE 0.15

/* mod x to SIZE, handling negative values correctly */
#define MOD(x) ((x+SIZE) % SIZE)

#define FOR(x,dx) int x = 0; x < SIZE; x += dx

typedef h_t grid[SIZE][SIZE];

typedef struct {
  grid land;
  grid water;
  grid tide;
  grid flow;
  grid buffer;
} state_t;

long int t = 0;
state_t state;

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

void init_state() {
  generate_land(state.land);
  for (FOR(x,1)) {
    for (FOR(y,1)) {
      state.water[x][y] = DEPTH;
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
  memset(state.flow, 0, sizeof(h_t)*SIZE*SIZE);
  for (FOR(x,1)) {
    for (FOR(y,1)) {
      state.tide[x][y] = TIDE_AMP * sin((x + t*TIDE_RATE) * M_PI * 2 / SIZE);
    }
  }

  for (FOR(x,1)) {
    for (FOR(y,1)) {

      for (int dx = -1; dx <= 1; dx++) {
	for (int dy = -1; dy <= 1; dy++) {
	  if (!dx != !dy) { //XOR
	    h_t f = (
		     (state.land[x][y]+state.tide[x][y]+state.buffer[x][y])-
		     (state.land[MOD(x+dx)][MOD(y+dy)]+state.tide[MOD(x+dx)][MOD(y+dy)]+state.buffer[MOD(x+dx)][MOD(y+dy)])
		     ) / 5;
	    if (f > 0) {
	      h_t mf = state.buffer[x][y] / 4;
	      f = f <= mf ? f : mf;

	      state.water[x][y] -= f;
	      state.water[MOD(x+dx)][MOD(y+dy)] += f;
	      state.flow[x][y] += f;
	    }
	  }
	}
      }
      
      state.water[x][y] *= DECAY;
      state.water[x][y] += RAIN;
    }
  }
}

void update_state() {
  flow_water();
  t += 1;
}

void render_state() {
  for (FOR(x,1)) {
    for (FOR(y,1)) {
      h_t water_scaled = state.water[x][y]/SIZE;
      h_t water_alpha = exp(-state.water[x][y]*35);
      h_t scaled = atan(state.land[x][y]*2 / SIZE) / M_PI + 0.5;
      h_t light = atan(
			(state.land[x][y]+state.water[x][y])-
			(state.land[x][MOD(y-1)]+state.water[x][MOD(y-1)])
			) / M_PI + 0.5;
      unsigned char color[3];
      /* blue */  color[0] = (unsigned char)((1 - water_alpha)*light * 255);
      /* green */ color[1] = (unsigned char)((1-scaled)*water_alpha*light * 255);
      /* red */   color[2] = (unsigned char)(scaled*water_alpha*light * 255);

      for (int zx = 0; zx < ZOOM; zx++) {
	for (int zy = 0; zy < ZOOM; zy++) {
	  for (int rgb = 0; rgb < 3; rgb++) {
	    pixels[y*ZOOM+zy][x*ZOOM+zx][rgb] = color[rgb];
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

    render_state();
    render_to_screen();

    // printf("%lu\n", (clock() - ct) * 1000 / CLOCKS_PER_SEC);
    ct = clock();
  }

  teardown_sdl_stuff();

  return 0;
}
