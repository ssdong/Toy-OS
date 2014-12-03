#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>

/* Cats and Mice synchronization problem  */

/* 
 * This simple default synchronization mechanism allows only creature at a time to
 * eat.   The globalCatMouseSem is used as a a lock.   We use a semaphore
 * rather than a lock so that this code will work even before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
static struct lock** lk_for_bowls;
static struct lock* set_catmouse_eat;
static struct cv* mouse_sleep;
static struct cv* cat_sleep;
static volatile int catsEating;
static volatile int catsSleeping;
static volatile int mouseEating;
static volatile int mouseSleeping;
static volatile int cat_mouse_turn;


/* 
 * The CatMouse simulation will call this function once before any cat or
 * mouse tries to each.
 *
 * You can use it to initialize synchronization and other variables.
 * 
 * parameters: the number of bowls
 */
void
catmouse_sync_init(int bowls)
{
  // Initialize locks
  set_catmouse_eat = lock_create("set_catmouse_eat");
  if(set_catmouse_eat == NULL) {
    panic("could not create global setCatMouseEat synchronization lock");
  }

  lk_for_bowls = kmalloc(sizeof(struct lock*) * bowls);
  for (int i = 0; i < bowls; i++) {
    lk_for_bowls[i] = lock_create("bowl_lock");
    if(lk_for_bowls[i] == NULL) {
      panic("could not create locks for bowls");
    }
  }

  // Initialize cv
  cat_sleep = cv_create("cat_sleep");
  if(cat_sleep == NULL) {
    panic("could not create global catSleep synchronization condition variable");
  }
  mouse_sleep = cv_create("mouse_sleep");
  if(mouse_sleep == NULL) {
    panic("could not create global mouseSleep synchronization condition variable");
  }

  catsEating = mouseEating = catsSleeping = mouseSleeping = 0;
  cat_mouse_turn = -1;

  return;
}

/* 
 * The CatMouse simulation will call this function once after all cat
 * and mouse simulations are finished.
 *
 * You can use it to clean up any synchronization and other variables.
 *
 * parameters: the number of bowls
 */
void
catmouse_sync_cleanup(int bowls)
{
  lock_destroy(set_catmouse_eat);
  for(int i = 0; i < bowls; i++) {
    lock_destroy(lk_for_bowls[i]);
  }
  cv_destroy(cat_sleep);
  cv_destroy(mouse_sleep);
}


/*
 * The CatMouse simulation will call this function each time a cat wants
 * to eat, before it eats.
 * This function should cause the calling thread (a cat simulation thread)
 * to block until it is OK for a cat to eat at the specified bowl.
 *
 * parameter: the number of the bowl at which the cat is trying to eat
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
cat_before_eating(unsigned int bowl) 
{
  // If we have mice eating bowls right now
  lock_acquire(set_catmouse_eat);
  if(cat_mouse_turn == -1) {
      cat_mouse_turn = 1;
  }
  lock_release(set_catmouse_eat);

  lock_acquire(lk_for_bowls[bowl - 1]);
  lock_acquire(set_catmouse_eat);
  while(cat_mouse_turn == 0 || mouseEating > 0) {
      catsSleeping += 1;
      lock_release(set_catmouse_eat);
      cv_wait(cat_sleep, lk_for_bowls[bowl - 1]); // Cat sleep !
      lock_acquire(set_catmouse_eat);
      catsSleeping -= 1;
  }
  catsEating += 1;
  lock_release(set_catmouse_eat);
}

/*
 * The CatMouse simulation will call this function each time a cat finishes
 * eating.
 *
 * You can use this function to wake up other creatures that may have been
 * waiting to eat until this cat finished.
 *
 * parameter: the number of the bowl at which the cat is finishing eating.
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
cat_after_eating(unsigned int bowl) 
{
  lock_acquire(set_catmouse_eat);
  catsEating -= 1;
  if(catsEating == 0 && mouseSleeping > 0) {
     cat_mouse_turn = 0;
     cv_broadcast(mouse_sleep, lk_for_bowls[bowl - 1]);
  }
  else if(catsEating == 0 && mouseSleeping == 0 && catsSleeping == 0 ) {
      cat_mouse_turn = -1;
  }
  else {
     cv_broadcast(cat_sleep, lk_for_bowls[bowl - 1]);
  }
  lock_release(set_catmouse_eat);
  lock_release(lk_for_bowls[bowl - 1]);
}

/*
 * The CatMouse simulation will call this function each time a mouse wants
 * to eat, before it eats.
 * This function should cause the calling thread (a mouse simulation thread)
 * to block until it is OK for a mouse to eat at the specified bowl.
 *
 * parameter: the number of the bowl at which the mouse is trying to eat
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
mouse_before_eating(unsigned int bowl) 
{
  // If we have mice eating bowls right now
  lock_acquire(set_catmouse_eat);
  if(cat_mouse_turn == -1) {
      cat_mouse_turn = 0;
  }
  lock_release(set_catmouse_eat);

  lock_acquire(lk_for_bowls[bowl - 1]);
  lock_acquire(set_catmouse_eat);
  while(cat_mouse_turn == 1 || catsEating > 0) {
      mouseSleeping += 1;
      lock_release(set_catmouse_eat);
      cv_wait(mouse_sleep, lk_for_bowls[bowl - 1]); // Cat sleep !
      lock_acquire(set_catmouse_eat);
      mouseSleeping -= 1;
  }
  mouseEating += 1;
  lock_release(set_catmouse_eat);
}

/*
 * The CatMouse simulation will call this function each time a mouse finishes
 * eating.
 *
 * You can use this function to wake up other creatures that may have been
 * waiting to eat until this mouse finished.
 *
 * parameter: the number of the bowl at which the mouse is finishing eating.
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
mouse_after_eating(unsigned int bowl) 
{ 
  lock_acquire(set_catmouse_eat);
  mouseEating -= 1;
  if(mouseEating == 0 && catsSleeping > 0) {
     cat_mouse_turn = 1;
     cv_broadcast(cat_sleep, lk_for_bowls[bowl - 1]);
  }
  else if(mouseEating == 0 && catsSleeping == 0 && mouseSleeping == 0) {
     cat_mouse_turn = -1;
  }
  else {
     cv_broadcast(mouse_sleep, lk_for_bowls[bowl - 1]);
  }
  lock_release(set_catmouse_eat);
  lock_release(lk_for_bowls[bowl - 1]);
}
