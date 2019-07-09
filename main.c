#include <stdio.h>
#include <pthread.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

/**********************************************************************/
/* Random-number generator                                            */

/* This is a stripped-down version of random.c as distributed with
   FreeBSD 2.2 */

#define	DEG		31
#define	SEP		3

int fd[2];

pthread_mutex_t lock;

typedef struct rand_state_t {
  short fpos, rpos;
  int state[DEG];
} rand_state_t;

static int bsd_rand(int n, rand_state_t *r)
{
  int i;
	pthread_mutex_lock(&lock);
  r->state[r->fpos] += r->state[r->rpos];
  i = (r->state[r->fpos] >> 1) & 0x7fffffff; /* chucking least random bit */
	pthread_mutex_unlock(&lock);
  if (++(r->fpos) >= DEG) {
    r->fpos = 0;
    r->rpos++;
  } else if (++(r->rpos) >= DEG)
    r->rpos = 0;
  return i % n;
}

static void bsd_srand(int x, rand_state_t *r)
{
  register int i;

  r->state[0] = x & 0x7fffffff;
  for (i = 1; i < DEG; i++) {
    register long hi, lo, v;
  
    v = r->state[i - 1];
    hi = v / 127773;
    lo = v % 127773;
    v = 16807 * lo - 2836 * hi;
    if (v <= 0)
      v += 0x7fffffff;
    r->state[i] = v;
  }
  r->fpos = SEP;
  r->rpos = 0;
  for (i = 0; i < 10 * DEG; i++) {
    (void)bsd_rand(1, r);
  }
}

/**********************************************************************/
/* Game configuration                                                 */

/* End game after this many moves: */
#define MAX_MOVES 500000

/* Show field roughly once per epoch: */
#define MOVES_PER_EPOCH (MAX_MOVES / 10)

/* Counters: */
static int moves = 0;
static int epochs = 0;

/* Random-number state: */
static rand_state_t r;


/* Game stats (per team): */
static int passes[2] = { 0, 0 };
static int steals[2] = { 0, 0 };

/* Field size: */
#define HSIZE 76
#define VSIZE 20
static struct player_t *field[VSIZE][HSIZE];

#define NUM_PLAYERS 4
#define NUM_BALLS 2

static const int RED_TEAM = 0;
static const int BLUE_TEAM = 1;

typedef struct player_t {
  int rank;
  int team;
  int field_x, field_y;
  struct ball_t *ball; /* NULL => not carrying a ball */
	struct rand_state_t r;
  pthread_t task;
} player_t;

typedef struct ball_t {
  int field_x, field_y;
  player_t *carried_by; /* NULL => directly on field */
} ball_t;

static player_t red[NUM_PLAYERS];
static player_t blue[NUM_PLAYERS];
static ball_t ball[NUM_BALLS];

/**********************************************************************/
/* Ball-transfer contest                                              */

static int contest(player_t *p1, player_t *p2)

{
  int c1 = 0, c2 = 0;
	
  while ((c1 < 10000) && (c2 < 10000)) {
    if (bsd_rand(100, &r) > bsd_rand(100, &r)){
			pthread_mutex_lock(&lock);
      c1++;
			pthread_mutex_unlock(&lock);
			}
    else{
			pthread_mutex_lock(&lock);
      c2++;
			pthread_mutex_unlock(&lock);
		}
  }

  return (c1 > c2);
}

/**********************************************************************/
/* The player thread's procedure                                      */

void *run_player(void *_p) 
{
	
  player_t *p = (player_t *)_p, *p2;
	
  while (1) {
		
    int x, y, result;

    x = p->field_x;
    y = p->field_y;
    switch (bsd_rand(4, &r)) {
    case 0: pthread_mutex_lock(&lock);x++;pthread_mutex_unlock(&lock); break;
    case 1: pthread_mutex_lock(&lock);--x;pthread_mutex_unlock(&lock); break;
    case 2: pthread_mutex_lock(&lock);y++;pthread_mutex_unlock(&lock); break;
    case 3: pthread_mutex_lock(&lock);y--;pthread_mutex_unlock(&lock); break;
    }
     
    if ((x >= 0) && (x < HSIZE)
        && (y >= 0) && (y < VSIZE)) {

      if (moves >= MAX_MOVES)
        return NULL;
			pthread_mutex_lock(&lock);
      moves++;
			pthread_mutex_unlock(&lock);
      if ((moves % MOVES_PER_EPOCH) == 0) {
        epochs++;
				result = write(fd[1], &epochs, 1);
      }

      p2 = field[y][x];
      if (!p2) {
        /* move */
				pthread_mutex_lock(&lock);

        field[p->field_y][p->field_x] = NULL;
        field[y][x] = p;
        p->field_x = x;
        p->field_y = y;

				pthread_mutex_unlock(&lock);
        if (p->ball) {
					pthread_mutex_lock(&lock);
          p->ball->field_x = x;
          p->ball->field_y = y;
					pthread_mutex_unlock(&lock);
        }
      } else {
        if (p->ball && (p->team == p2->team) && !p2->ball) {
          /* try to pass */
          if (contest(p, p2)) {
            /* a pass */
						pthread_mutex_lock(&lock);
            p2->ball = p->ball;
            p->ball->field_x = x;
            p->ball->field_y = y;
            p->ball->carried_by = p2;
            p->ball = NULL;
            passes[p->team]++;
						pthread_mutex_unlock(&lock);
          }
        } else if (!p->ball && (p->team != p2->team) && p2->ball) {
          /* try to steal... */
          if (contest(p, p2)) {
            /* a steal */
						pthread_mutex_lock(&lock);
            p->ball = p2->ball;
            p2->ball = NULL;
            p->ball->field_x = p->field_x;
            p->ball->field_y = p->field_y;
            p->ball->carried_by = p;
            steals[p->team]++;
						pthread_mutex_unlock(&lock);
          }
        } else {
          /* bump */
        }
      }
    }
  }
}

/**********************************************************************/
/* Showing the field                                                  */

static void show_field()
{
  int i, j;

  printf("\nMoves: %d  Red passes+steals: %d+%d  Blue passes+steals: %d+%d\n", 
         moves, 
         passes[RED_TEAM], steals[RED_TEAM], 
         passes[BLUE_TEAM], steals[BLUE_TEAM]);
  for (i = 0; i < VSIZE; i++) {
    for (j = 0; j < HSIZE; j++) {
			pthread_mutex_lock(&lock);
      player_t *p = field[i][j];
			pthread_mutex_unlock(&lock);
      if (p) {
        char c;
        c = ((p->team == RED_TEAM) ? 'r' : 'b');
        if (p->ball)
          c = toupper(c);
        printf("%c", c);
        /* Sanity check: */
        if (p->ball)
          if (p->ball->carried_by != p) {
            fprintf(stderr, "Player has ball, but ball doesn't refer to player\n");
            abort();
          }
      } else
        printf("_");
    }
    printf("\n");
  }

  /* More sanity checks: */
	pthread_mutex_lock(&lock);
  for (i = 0; i < NUM_PLAYERS; i++) {
    if (field[red[i].field_y][red[i].field_x] != &red[i]) {
      fprintf(stderr, "A red player's position doesn't match the field\n");
      abort();
    }
    if (field[blue[i].field_y][blue[i].field_x] != &blue[i]) {
      fprintf(stderr, "A blue player's position doesn't match the field\n");
      abort();
    }
  }
  for (i = 0; i < NUM_BALLS; i++) {
    if (ball[i].carried_by->ball != &ball[i]) {
      fprintf(stderr, "Ball's carrier does not refer to ball\n");
      abort();
    }
    if ((ball[i].field_x != ball[i].carried_by->field_x)
        || (ball[i].field_y != ball[i].carried_by->field_y)) {
      fprintf(stderr, "Ball is not at its carrier's position\n");
      abort();
    }
  }
	pthread_mutex_unlock(&lock);
}

/**********************************************************************/
/* Main                                                               */

int main()
{
	if (pthread_mutex_init(&lock, NULL) != 0)
	{
		printf("\n mutex init failed\n");
		return 1;
	}
  int i, mix, result, epoch;
  int prev_shown_epochs = 0;

	result = pipe(fd);
	//epoch = read(fd[0], &epochs, 1);

  mix = (int)time(NULL);

  bsd_srand(mix, &r);

	
  /* Put players on the field: */
  for (i = 0; i < NUM_PLAYERS; i++) {
		pthread_mutex_lock(&lock);
    red[i].rank = 2*i;
    red[i].field_x = i;
    red[i].field_y = 0;
    red[i].team = RED_TEAM;
    red[i].ball = NULL;	

    field[0][i] = &red[i];

    blue[i].rank = 2*i + 1;
    blue[i].field_x = i;
    blue[i].field_y = VSIZE-1;
    blue[i].team = BLUE_TEAM;
    blue[i].ball = NULL;

    field[VSIZE-1][i] = &blue[i];
		pthread_mutex_unlock(&lock);
  }

  /* Give one ball to each team: */
  ball[0].carried_by = &red[0];
  ball[1].carried_by = &blue[0];
  for (i = 0; i < NUM_BALLS; i++) {
    ball[i].carried_by->ball = &ball[i];
    ball[i].field_x = ball[i].carried_by->field_x;
    ball[i].field_y = ball[i].carried_by->field_y;
  }

  /* Start the players: */
  for (i = 0; i < NUM_PLAYERS; i++) {
    pthread_create(&red[i].task, NULL, run_player, &red[i]);
    pthread_create(&blue[i].task, NULL, run_player, &blue[i]);
  }

  /* Update field display occassionally: */
  while (moves < MAX_MOVES) {
    if (epochs > prev_shown_epochs) {
      show_field();
			pthread_mutex_lock(&lock);
      prev_shown_epochs = epochs;
			pthread_mutex_unlock(&lock);
    }
  }

  /* Game over. Wait for players to stop, then show field
     one last time (just in case). */

  printf("Game over\n");

  for (i = 0; i < NUM_PLAYERS; i++) {
    pthread_join(red[i].task, NULL);
    pthread_join(blue[i].task, NULL);
  }
	pthread_mutex_destroy(&lock);

  show_field();

  return 0;
}
