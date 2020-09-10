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
  grid temp;
  grid latitude;
  grid vapor;
  grid rain;
  grid buf;
} state_t;

typedef struct {
#include "conf_decl.c"
} conf_t;

typedef struct {
  int pal;
  int vx;
  int vy;
  double theta;
  double phi;
  double zoom;
  double hscale;
  int offset;
} view_t;

long int t = 0;
state_t state;
conf_t conf;
view_t view;

void parse_conf_line(const char line[]) {
  if (line[0] == '#') { return; }
  char key[256] = { 0 };
  char val[256] = { 0 };

  int len = strlen(line);
  char *pos = strchr(line, '=');
  if (pos == NULL) {
    printf("Bad conf line: %s", line);
    exit(1);
  }
  strncpy(key, line, pos - line);
  strncpy(val, pos + 1, line + len - pos);

  if (0) { }
#include "conf_parse.c"
  else {
    printf("Unknown conf key: %s\n", key);
    exit(1);
  }
}

void parse_conf(const char *filename) {
  char line[256] = { 0 };
  FILE *fp = fopen(filename, "r");
  if (fp == NULL) {
    printf("Unable to read conf file %s\n", filename);
    exit(1);
  }
  while (!feof(fp)) {
    memset(line, 0, 256);
    fgets(line, 256, fp);
    if (strlen(line)) { parse_conf_line(line); }
  }
  fclose(fp);
}

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
      state.temp[x][y] = (
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
      state.vapor[x][y] = equilibrium_vapor(state.temp[x][y]);
    }
  }
}

SDL_Window *sdl_win;
SDL_Renderer *sdl_ren;
SDL_Texture *sdl_tex;

unsigned char mappixels[SIZE][SIZE][4];
unsigned char screenpixels[SIZE*ZOOM][SIZE*ZOOM*2][4];
unsigned char clickpixels[SIZE*ZOOM*2][SIZE*ZOOM][3];

void setup_sdl_stuff() {
  SDL_Init(SDL_INIT_VIDEO);

  sdl_win = SDL_CreateWindow("Watershed", 0, 0, SIZE*ZOOM*2, SIZE*ZOOM, 0);
  sdl_ren = SDL_CreateRenderer(sdl_win, -1, 0);
  sdl_tex = SDL_CreateTexture(sdl_ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STATIC, SIZE*ZOOM*2, SIZE*ZOOM);

  memset(screenpixels, 0, SIZE*ZOOM*2 * SIZE*ZOOM * 4);
  memset(clickpixels, 0, SIZE*ZOOM*2 * SIZE*ZOOM * 3);
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
      h_t eq_vap = equilibrium_vapor(state.temp[x][y]);
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
  memcpy(state.buf, state.vapor, sizeof(h_t)*SIZE*SIZE);

  for (FOR(x)) {
    for (FOR(y)) {

      state.vapor[x][y] = state.buf[x][y] * (((h_t)VAP_DIFF - 4) / (h_t)VAP_DIFF);

      for (int dx = -1; dx <= 1; dx++) {
	for (int dy = -1; dy <= 1; dy++) {
	  if (!dx != !dy) { //XOR
	    state.vapor[x][y] += state.buf[MOD(x+dx)][MOD(y+dy)] / VAP_DIFF;
	  }
	}
      }
    }
  }

  if (WIND_SP != 0) {
    int wdir = (WIND_SP > 0) - (WIND_SP < 0);

    memcpy(state.buf, state.vapor, sizeof(h_t)*SIZE*SIZE);

    for (FOR(x)) {
      for (FOR(y)) {
	state.vapor[x][y] = WIND_SP * state.buf[MOD(x+wdir)][y] + (1.0 - WIND_SP) * state.buf[x][y];
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

void render_state() {
  memset(screenpixels, 0, SIZE*ZOOM*2 * SIZE*ZOOM * 4);
  memset(clickpixels, 0, SIZE*ZOOM*2 * SIZE*ZOOM * 3);

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

      switch(view.pal) {

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

      for (int rgb = 0; rgb < 3; rgb++) {
	mappixels[x][y][rgb] = color[rgb];
      }
    }
  }

  // blit main map
  for (FOR(x)) {
    for (FOR(y)) {
      for (int zx = 0; zx < ZOOM; zx++) {
	for (int zy = 0; zy < ZOOM; zy++) {
	  for (int rgb = 0; rgb < 3; rgb++) {
	    screenpixels[MOD(y-view.vy)*ZOOM+zy][MOD(x-view.vx)*ZOOM+zx][rgb] = mappixels[x][y][rgb];
	  }
	  clickpixels[MOD(x-view.vx)*ZOOM+zx][MOD(y-view.vy)*ZOOM+zy][0] = 1;
	  clickpixels[MOD(x-view.vx)*ZOOM+zx][MOD(y-view.vy)*ZOOM+zy][1] = x;
	  clickpixels[MOD(x-view.vx)*ZOOM+zx][MOD(y-view.vy)*ZOOM+zy][2] = y;
	}
      }
    }
  }
  
  // render detail map
  int detailwidth = SIZE*ZOOM/view.zoom;
  int sdx = (sin(view.theta) < 0) ? -1 : 1;
  int sdy = (cos(view.theta) < 0) ? -1 : 1;
  double sintheta = sin(view.theta);
  double costheta = cos(view.theta);
  double sinphi = sin(view.phi);
  double cosphi = cos(view.phi);
  double zx = 0.5*(sintheta*sdx + costheta*sdy)*view.zoom;
  double zy = 0.5*cosphi*(sintheta*sdx + costheta*sdy)*view.zoom;

  for (int dy = -detailwidth*sdy; dy*sdy <= detailwidth; dy += sdy) {
    for (int dx = -detailwidth*sdx; dx*sdx <= detailwidth; dx += sdx) {

      int x = MOD(view.vx + dx + SIZE/2);
      int y = MOD(view.vy + dy + SIZE/2);
      h_t h = state.land[x][y] + state.water[x][y];
      h_t hdx = state.land[MOD(x+sdx)][y] + state.water[MOD(x+sdx)][y];
      h_t hdy = state.land[x][MOD(y+sdy)] + state.water[x][MOD(y+sdy)];
      h_t hd = (hdx < hdy) ? hdx : hdy; 

      int px = SIZE*ZOOM*1.5 + view.zoom*(dx*costheta-dy*sintheta);
      int py = SIZE*ZOOM*0.5 + view.offset + view.zoom*((dx*sintheta+dy*costheta)*cosphi - h*view.hscale*sinphi);

      double zhy = (h - hd)*view.hscale*sinphi*view.zoom;
      if (zhy < 0) { zhy = 0; }

      int from_x = px - zx;
      int to_x = px + zx + 0.5;
      int from_y = py - zy;
      int to_y = py + zy + zhy + 1.5;

      from_x = (from_x < SIZE*ZOOM+1) ? SIZE*ZOOM+1 : from_x;
      to_x = (to_x >= SIZE*ZOOM*2) ? SIZE*ZOOM*2-1 : to_x;
      from_y = (from_y < 0) ? 0 : from_y;
      to_y = (to_y >= SIZE*ZOOM) ? SIZE*ZOOM-1 : to_y;

      for (int rx = from_x; rx <= to_x; rx++) {
	int cutoff = 0;
	int sdrx = (px - rx)*sdx;
	for (int ry = from_y; ry <= to_y; ry++) {
	  int sdry = (py - ry)*sdy;
	  if (cutoff ||
	      ((sdx == sdy) &&
	       sdry*sintheta/cosphi + sdrx*costheta <= view.zoom*0.5 + 1 &&
	       sdry*costheta/cosphi - sdrx*sintheta <= view.zoom*0.5 + 1) ||
	      ((sdx != sdy) &&
	       sdry*costheta/cosphi + sdrx*sintheta <= view.zoom*0.5 + 1 &&
	       -sdry*sintheta/cosphi + sdrx*costheta <= view.zoom*0.5 + 1)) {
	    cutoff = 1;
	    for (int rgb = 0; rgb < 3; rgb++) {
	      screenpixels[ry][rx][rgb] = mappixels[x][y][rgb];
	    }
	    clickpixels[rx][ry][0] = 1;
	    clickpixels[rx][ry][1] = x;
	    clickpixels[rx][ry][2] = y;
	  }
	}
      }
    }
  }
}

void render_to_screen() {
  SDL_UpdateTexture(sdl_tex, NULL, screenpixels, SIZE*ZOOM*2*4);
  SDL_RenderClear(sdl_ren);
  SDL_RenderCopy(sdl_ren, sdl_tex, NULL, NULL);
  SDL_RenderPresent(sdl_ren);
}

int main(int argc, char* argv[])
{
  long seed = time(NULL);
  int pause = 0;
  int quit = 0;

  view.pal = PAL_ALT;
  view.theta = 0.25;
  view.phi = 0.75;
  view.zoom = 4.0;
  view.offset = 40;
  view.hscale = 0.5;

  int mousex = 0;
  int mousey = 0;
  int rightbutton = 0;

  if (argc > 1) { seed = atol(argv[1]); }
  printf("%lu\n", seed);
  parse_conf("default.conf");
  printf("%i\n", conf.dummy);
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

      case SDL_MOUSEBUTTONDOWN:
	switch (e.button.button) {

	case SDL_BUTTON_LEFT:
	  if (clickpixels[e.button.x][e.button.y][0]) {
	    state.water[clickpixels[e.button.x][e.button.y][1]][clickpixels[e.button.x][e.button.y][2]] = SIZE;
	  }
	  break;

        case SDL_BUTTON_RIGHT:
	  rightbutton = 1;
	  if (e.button.x > SIZE*ZOOM) { rightbutton = 2; }
	  break;
	}
	break;

      case SDL_MOUSEBUTTONUP:
	switch (e.button.button) {

        case SDL_BUTTON_RIGHT:
	  rightbutton = 0;
	  break;
	}
	break;

      case SDL_MOUSEMOTION:
	if (rightbutton == 1) {
	  view.vx = MOD(view.vx + (mousex - e.motion.x)/ZOOM);
	  view.vy = MOD(view.vy + (mousey - e.motion.y)/ZOOM);
	}
	else if (rightbutton == 2) {
	  view.theta += (mousex - e.motion.x)*2.0/(SIZE*ZOOM);
	  if (view.theta < 0) { view.theta += 2*M_PI; }
	  if (view.theta > 2*M_PI) { view.theta -= 2*M_PI; }

	  view.phi += (mousey - e.motion.y)*2.0/(SIZE*ZOOM);
	  if (view.phi > 1.5) { view.phi = 1.5; }
	  if (view.phi < 0.1) { view.phi = 0.1; }
	}
	mousex = e.motion.x;
	mousey = e.motion.y;
	break;

      case SDL_MOUSEWHEEL:
	view.zoom *= exp(e.wheel.y * 0.025);
	if (view.zoom < ZOOM*2) { view.zoom = ZOOM*2; }
	if (view.zoom > 32) { view.zoom = 32; }
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
	  view.pal = PAL_ALT;
	  break;
	case SDLK_s:
	  view.pal = PAL_BIOME;
	  break;
	case SDLK_d:
	  view.pal = PAL_FLOW;
	  break;
	case SDLK_f:
	  view.pal = PAL_MOMENT;
	  break;
	}
      }
    }

    if (pause) {
      SDL_Delay(150);
    }
    else {
      update_state();
    }

    if (t % 1 == 0) {
      render_state();
      render_to_screen();
    }

    if (0) {
      printf("%lu\n", (clock() - ct) * 1000 / CLOCKS_PER_SEC);
    }

    ct = clock();
  }

  teardown_sdl_stuff();

  return 0;
}
