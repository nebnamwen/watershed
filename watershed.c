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

typedef h_t grid[SIZE][SIZE];

typedef struct {
  grid land;
  grid water;
  grid tide;
  grid xflow;
  grid yflow;
  grid temp;
  grid vapor;
  grid rain;
  grid buf;
} state_t;

typedef struct {
#include "conf_decl.c"
} conf_t;

typedef struct {
  int pal;
  int skip;
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

void parse_conf_line(const char line[], const char *filename) {
  if (strchr("#\n", line[0]) != NULL) { return; }
  char key[256] = { 0 };
  char val[256] = { 0 };

  int len = strlen(line);
  char *pos = strchr(line, '=');
  if (pos == NULL) {
    if (strlen(filename)) { printf("(%s) ", filename); }
    printf("Bad conf line: %s", line);
    exit(1);
  }
  strncpy(key, line, pos - line);
  strncpy(val, pos + 1, line + len - pos);

  if (0) { }
#include "conf_parse.c"
  else {
    if (strlen(filename)) { printf("(%s) ", filename); }
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
    if (strlen(line)) { parse_conf_line(line, filename); }
  }
  fclose(fp);
}

h_t equilibrium_vapor(h_t temp) {
  return conf.vap_exc_pstd * exp(conf.vap_exc_htemp * temp);
}

void generate_land_point(grid g, int x, int y, int octave) {
  h_t skew = 0.0;

  octave = (octave <= conf.tgen_seed_oct) ? conf.tgen_seed_oct : octave;

  int du = SIZE >> ((octave + 1)/2);
  int dv = du * (octave % 2);
  h_t octave_scale = sqrt(du*du + dv*dv);
  h_t base = conf.tgen_hbase * conf.tgen_hscale * octave_scale;

  if (octave > conf.tgen_seed_oct) {
    h_t a = g[MOD(x+du)][MOD(y+dv)];
    h_t b = g[MOD(x+du)][MOD(y-dv)];
    h_t c = g[MOD(x-du)][MOD(y-dv)];
    h_t d = g[MOD(x-dv)][MOD(y+du)];

    base = (a + b + c + d) / 4.0;
    skew = cbrt(
		(
		 pow(a - base, 3) +
		 pow(b - base, 3) +
		 pow(c - base, 3) +
		 pow(d - base, 3)
		 ) / 4.0
		);
  }

  h_t noise = (h_t)rand()/(h_t)RAND_MAX;
  h_t disp = (noise - 0.5) * 2;
  g[x][y] = base + disp * octave_scale * conf.tgen_hscale + skew * conf.tgen_skew;
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
      state.temp[x][y] = -(state.land[x][y]+state.water[x][y]);
    }
  }  
}

void init_state(long seed) {
  generate_land(state.land, seed);
  for (FOR(x)) {
    for (FOR(y)) {
      state.water[x][y] = - state.land[x][y];
      state.water[x][y] *= state.water[x][y] > 0;
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
      state.tide[x][y] = conf.tide_amp * sin((x * 1.0 / SIZE + (t % conf.tide_period) * 1.0 / conf.tide_period) * 2 * M_PI);
    }
  }

  // update flow field
  for (FOR(x)) {
    for (FOR(y)) {

      for (int dx = 0; dx <= 1; dx++) {
	for (int dy = 0; dy <= 1; dy++) {
	  if (!dx != !dy) { // XOR
	    h_t dh =
	      (state.land[x][y]+state.tide[x][y]+state.water[x][y])-
	      (state.land[MOD(x+dx)][MOD(y+dy)]+state.tide[MOD(x+dx)][MOD(y+dy)]+state.water[MOD(x+dx)][MOD(y+dy)]);

	    int fromx = (dh > 0) ? x : MOD(x+dx);
	    int fromy = (dh > 0) ? y : MOD(y+dy);

	    h_t dP = conf.flow_gravity * state.water[fromx][fromy];
	    dP = 0.475 * sin(atan(dP / 0.475)); // 0.5 is the threshold for stability

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
	  if (!dx != !dy) { // XOR
	    int flowx = (dx < 0) ? MOD(x-1) : x;
	    int flowy = (dy < 0) ? MOD(y-1) : y;
	    outflow += (state.xflow[flowx][flowy] * dx > 0) ? state.xflow[flowx][flowy] * dx : 0;
	    outflow += (state.yflow[flowx][flowy] * dy > 0) ? state.yflow[flowx][flowy] * dy : 0;
	  }
	}
      }

      if (outflow > 0) {
	h_t clamp = state.water[x][y] * conf.flow_clamp / outflow;
	clamp = (clamp > 1.0) ? 1.0 : clamp;

	for (int dx = -1; dx <= 1; dx++) {
	  for (int dy = -1; dy <= 1; dy++) {
	    if (!dx != !dy) { // XOR
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
      state.xflow[x][y] *= conf.flow_damp;
      state.yflow[x][y] *= conf.flow_damp;

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
      h_t delta = state.vapor[x][y] - eq_vap;
      h_t rain = delta * (delta > 0 ? conf.vap_exc_cond : conf.vap_exc_evap);
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

      state.vapor[x][y] = state.buf[x][y] * (1.0 - conf.vap_flow_diff * 4);

      for (int dx = -1; dx <= 1; dx++) {
	for (int dy = -1; dy <= 1; dy++) {
	  if (!dx != !dy) { // XOR
	    state.vapor[x][y] += state.buf[MOD(x+dx)][MOD(y+dy)] * conf.vap_flow_diff;
	  }
	}
      }
    }
  }

  double wphase = ((t % conf.vap_wind_period) * 1.0 / conf.vap_wind_period) * 2 * M_PI;
  h_t windx = conf.vap_wind_x0 * (1 + cos(wphase)) * 0.5 + conf.vap_wind_x1 * (1 - cos(wphase)) * 0.5 +
    (conf.vap_wind_y1 - conf.vap_wind_y0) * 0.5 * sin(wphase) * conf.vap_wind_circ;
  h_t windy = conf.vap_wind_y0 * (1 + cos(wphase)) * 0.5 + conf.vap_wind_y1 * (1 - cos(wphase)) * 0.5 +
    (conf.vap_wind_x0 - conf.vap_wind_x1) * 0.5 * sin(wphase) * conf.vap_wind_circ;

  int wxdir = (windx > 0) - (windx < 0);
  int wydir = (windy > 0) - (windy < 0);

  memcpy(state.buf, state.vapor, sizeof(h_t)*SIZE*SIZE);

  for (FOR(x)) {
    for (FOR(y)) {
      state.vapor[x][y] = (1.0 - windx * wxdir - windy * wydir) * state.buf[x][y] +
	windx * wxdir * state.buf[MOD(x+wxdir)][y] +
	windy * wydir * state.buf[x][MOD(y+wydir)];
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

      h_t scaled_alt = sin(atan(state.land[x][y]*pow(2.0,conf.tgen_seed_oct*0.5) * 2 / SIZE - 1.0)) / 2 + 0.5;
      h_t light = sin(atan((
			    (state.land[x][y]+state.water[x][y])-
			    (state.land[x][MOD(y-1)]+state.water[x][MOD(y-1)])
			    )/2)) * 0.45 + 0.5 + 0.1;

      double red = 0;
      double green = 0;
      double blue = 0;
      double alpha = 1.0;

#define WATER_ALPHA(SCALE) 1.0 - exp(-state.water[x][y]*SCALE)

#define PAL_LAYER(RED,GREEN,BLUE,LIGHT,ALPHA) alpha = (ALPHA);	\
      red = (1.0 - alpha)*red + alpha*(LIGHT)*(RED);		\
      green = (1.0 - alpha)*green + alpha*(LIGHT)*(GREEN);	\
      blue = (1.0 - alpha)*blue + alpha*(LIGHT)*(BLUE);

      switch(view.pal) {

      case PAL_ALT:
	PAL_LAYER( scaled_alt, 1 - scaled_alt, 0, light, 1.0 );
	PAL_LAYER( 0, 0, 1.0, light, WATER_ALPHA(10) );
	break;

      case PAL_BIOME:
	PAL_LAYER( 0.25, 0.25, 0.25, light, 1.0 );
	PAL_LAYER( 0.45, 0.2, 0.1, light, WATER_ALPHA(5000) );
	PAL_LAYER( 0.75, 0.75, 0, light, WATER_ALPHA(150) );
	PAL_LAYER( 0, 1.0, 0, light, WATER_ALPHA(55) );
	PAL_LAYER( 0, 0.75, 0.75, light, WATER_ALPHA(20) );
	PAL_LAYER( 0, 0, 1.0, light, WATER_ALPHA(5) );
	PAL_LAYER( 0, 0, 0.5, light, WATER_ALPHA(0.05) );
	break;

      case PAL_FLOW:
	PAL_LAYER( 0.2, 0.2, 0.2, light, 1.0 );
	h_t flow = sin(atan((fabs(state.xflow[x][y]) + fabs(state.yflow[x][y])) * 15 / (state.water[x][y] + 0.00001))) / 2;
	PAL_LAYER( flow, scaled_alt, 1.0, 1.0, WATER_ALPHA(10) );
	break;

      case PAL_MOMENT:
	PAL_LAYER( 0.2, 0.2, 0.2, light, 1.0 );
	h_t xmoment = sin(atan(state.xflow[x][y] * 15 / (state.water[x][y] + 0.00001)));
	h_t ymoment = sin(atan(state.yflow[x][y] * 15 / (state.water[x][y] + 0.00001)));
	PAL_LAYER( (-xmoment*0.33 - ymoment*0.17 + 0.5), (xmoment*0.33 - ymoment*0.17 + 0.5), (ymoment*0.5 + 0.5), 1.0, WATER_ALPHA(10) );
	break;

      }

      /* blue */  mappixels[x][y][0] = (unsigned char)(blue*255);
      /* green */ mappixels[x][y][1] = (unsigned char)(green*255);
      /* red */   mappixels[x][y][2] = (unsigned char)(red*255);
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

int main(int argc, char *argv[])
{
  int pause = 0;
  int quit = 0;

  view.skip = 1;
  view.pal = PAL_ALT;
  view.theta = 0.25;
  view.phi = 0.75;
  view.zoom = 4.0;
  view.offset = 40;
  view.hscale = 0.5;

  int mousex = 0;
  int mousey = 0;
  int rightbutton = 0;

  parse_conf("default.conf");

  for (int i = 1; i < argc; i++) {
    char *arg = argv[i];
    char *pos = strchr(arg, '=');
    if (pos == NULL) { parse_conf(arg); }
    else { parse_conf_line(arg, ""); }
  }
  if (conf.tgen_seed == 0) {
    conf.tgen_seed = time(NULL);
    printf("tgen_seed=%lu\n", conf.tgen_seed);
  }

  init_state(conf.tgen_seed);
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
	    // state.water[clickpixels[e.button.x][e.button.y][1]][clickpixels[e.button.x][e.button.y][2]] = SIZE;
	    int cx = clickpixels[e.button.x][e.button.y][1];
	    int cy = clickpixels[e.button.x][e.button.y][2];	    
	    printf("[%d,%d] land: %.2f; water: %.3f, xflow: %.3f; yflow: %.3f; vapor: %.3f\n", cx, cy, state.land[cx][cy], state.water[cx][cy], state.xflow[cx][cy], state.yflow[cx][cy], state.vapor[cx][cy]);
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

	// frameskip
	case SDLK_1:
	  view.skip = 1;
	  break;
	case SDLK_2:
	  view.skip = 2;
	  break;
	case SDLK_3:
	  view.skip = 4;
	  break;
	}
      }
    }

    if (pause) {
      SDL_Delay(30);
    }
    else {
      update_state();
    }

    if (t % view.skip == 0) {
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
