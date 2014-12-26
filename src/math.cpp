/*
 *  math.cpp
 *  Timelapse+
 *
 *  Created by Elijah Parker
 *  Copyright 2012 Timelapse+
 *  Licensed under GPLv3
 *
 */
 
//#include <stdio.h>
#include <inttypes.h>
#include <LUFA/Common/Common.h>

#include "math.h"
#include "debug.h"
#include "settings.h"

extern settings_t conf;

/******************************************************************
 *
 *   curve
 * 
 *   Catmull–Rom spline implementation
 *
 ******************************************************************/

float curve(float p0, float p1, float p2, float p3, float t)
{
    if(conf.linearInterpolation)
    {
      return t * (p2 - p1) + p1;
    }
    else
    {
      float t2 = t * t;
      float t3 = t2 * t;

      float ret = (float)
         ((2.0 * p1) +
          (p2 - p0) * t +
          (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
          ((3.0 * p1) - p0 - 3.0 * p2 + p3) * t3) / 2.0;

      return ret;
    }
}


uint32_t curve_int(uint32_t p0, uint32_t p1, uint32_t p2, uint32_t p3, float t)
{
    if(conf.linearInterpolation)
    {
      return (uint32_t)(t * (float)(p2 - p1)) + p1;
    }
    else
    {
      int64_t z = 1;
      const int64_t f1 = 1200, f2 = f1*f1, f3 = f2*f1;

      int64_t p0x = p0;
      int64_t p1x = p1;
      int64_t p2x = p2;
      int64_t p3x = p3;
      int64_t t1 = t > 0 ? (uint64_t)(1200.0 / t) : 0;

      if(t1 == 0) // Divide by zero fix
      {
        z = 0;
        t1 = 1;
      }

      int64_t t2 = t1 * t1;
      int64_t t3 = t2 * t1;

      int32_t ret = (int32_t)
         (((2 * p1x) +
          (p2x - p0x) * f1 / t1 * z +
          (2 * p0x - 5 * p1x + 4 * p2x - p3x) * f2 / t2 * z +
          ((3 * p1x) - p0x - 3 * p2x + p3x) * f3 / t3 * z) / 2);

      return ret;
    }
}

void sort(float *array, const uint8_t length)
{
  uint16_t num_swaps = 1; // sort the array
  while (num_swaps > 0) 
  { 
     num_swaps = 0; 
     for (uint8_t i = 1; i < length; i++) 
     { 
        if(array[i - 1] > array[i]) 
        {
          float temp = array[i];
          array[i] = array[i - 1];
          array[i - 1] = temp;
          num_swaps++; 
        } 
     } 
  }
}

void sortUInteger(uint16_t *array, const uint8_t length)
{
  uint16_t num_swaps = 1; // sort the array
  while (num_swaps > 0) 
  { 
     num_swaps = 0; 
     for (uint8_t i = 1; i < length; i++) 
     { 
        if(array[i - 1] > array[i]) 
        {
          uint16_t temp = array[i];
          array[i] = array[i - 1];
          array[i - 1] = temp;
          num_swaps++; 
        } 
     } 
  }
}

void sortInteger(int16_t *array, const uint8_t length)
{
  uint16_t num_swaps = 1; // sort the array
  while (num_swaps > 0) 
  { 
     num_swaps = 0; 
     for (uint8_t i = 1; i < length; i++) 
     { 
        if(array[i - 1] > array[i]) 
        {
          int16_t temp = array[i];
          array[i] = array[i - 1];
          array[i - 1] = temp;
          num_swaps++; 
        } 
     } 
  }
}

float arrayMedian(const float *array, const uint8_t length)
{
  float tmpArray[length];
  memcpy(tmpArray, array, length * sizeof(float));

  sort(tmpArray, length);

  float m;
  if(length % 2)
  {
    m = tmpArray[length / 2 + 1];
  }
  else
  {
    m = (tmpArray[length / 2] + tmpArray[length / 2 + 1]) / 2;
  }

  return m;
}

float arrayMedian50(const float *array, const uint8_t length)
{
  float tmpArray[length];
  memcpy(tmpArray, array, length * sizeof(float));

  sort(tmpArray, length);

  float m = 0.0, count = 0.0;
 
  for(uint8_t i = (length / 4); i < length - (length / 4); i++) // take the average of the middle two quarters
  {
    count++;
    m += tmpArray[i];
  }
  m /= count;
 
  return m;
}

uint16_t arrayMedian50UInt(const uint16_t *array, const uint8_t length)
{
  uint16_t tmpArray[length];
  memcpy(tmpArray, array, length * sizeof(uint16_t));

  sortUInteger(tmpArray, length);

  uint16_t m = 0.0, count = 0.0;
 
  for(uint8_t i = (length / 4); i < length - (length / 4); i++) // take the average of the middle two quarters
  {
    count++;
    m += tmpArray[i];
  }
  m /= count;
 
  return m;
}

int16_t arrayMedian50Int(const int16_t *array, const uint8_t length)
{
  int16_t tmpArray[length];
  memcpy(tmpArray, array, length * sizeof(int16_t));

  sortInteger(tmpArray, length);

  int16_t m = 0.0, count = 0.0;
 
  for(uint8_t i = (length / 4); i < length - (length / 4); i++) // take the average of the middle two quarters
  {
    count++;
    m += tmpArray[i];
  }
  m /= count;
 
  return m;
}





/*
    uint64_t ret = (uint64_t)
       ((2 * 500) +
        (2000 - 500) / 10 * 1 +
        (2 * 500 - 5 * 500 + 4 * 2000 - 2000) / 100 * 1 +
        ((3 * 500) - 500 - 3 * 2000 + 2000) / 1000 * 1) / 2;

    int64_t ret = (int64_t)
       (1000 +
        1500 / 10 +
        4500 / 100 +
        -3000 / 1000) / 2;

1500 / 10        = 150
1500 * 10 / 100  = 150

4500 / (10*10)        = 45
4500 * (10*10) / (100*100) = 45

*/
/*
int main()
{
  printf("Testing...\n\n");

  double i;

  for(i = 0; i <= 1.1; i += 0.1)
  {
    double y = curve(5, 5, 20, 20, i);
    int j;
    for (j = 0; j < (int) (y * 2.0); j++)
    {
      printf(" ");
    }
    printf("#\n");
  }
  int32_t error = 0;
  for(i = 0; i <= 1.0; i += 0.1)
  {
    double y = curve(100, 100, 419430400, 419430400, i);
    int32_t i2 = (int32_t) (i > 0 ? (1200)/i : 0);
    int32_t y2 = curve_int(100, 100, 419430400, 419430400, i2);
    printf(" y = %f   [%f]\n", y, i);
    //printf("y2 = %d   [%d]\n", y2, i2);
    error += (y2 - y) > 0 ? (y2 - y) : 0 - (y2 - y);
  }
  printf("Total error: %d\n", error);


  return 0;
}
//Error (1200) = 163514
//Error (1024) = 526963
//Error (1000) = 298874
//Error (256) = 1638583

//=((2.0 * C2) + (D2 - B2) * F2 + (2.0 * B2 - 5.0 * C2 + 4.0 * D2 - E2) * (F2*F2) + ((3.0 * C2) - B2 - 3.0 * D2 + E2) * (F2*F2*F2)) / 2.0


*/




