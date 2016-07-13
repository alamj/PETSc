#include <petsc/private/fortranimpl.h>
#include <petsc/private/dmdaimpl.h>

#if defined(PETSC_HAVE_FORTRAN_CAPS)
#define dmdagetghostcorners_           DMDAGETGHOSTCORNERS
#elif !defined(PETSC_HAVE_FORTRAN_UNDERSCORE)
#define dmdagetghostcorners_           dmdagetghostcorners
#endif

PETSC_EXTERN void PETSC_STDCALL  dmdagetghostcorners_(DM *da,PetscInt *x,PetscInt *y,PetscInt *z,PetscInt *m,PetscInt *n,PetscInt *p, int *ierr )
{
  PetscInt Y,Z,N,P;

  CHKFORTRANNULLINTEGER(y);
  CHKFORTRANNULLINTEGER(z);
  CHKFORTRANNULLINTEGER(n);
  CHKFORTRANNULLINTEGER(p);

  *ierr = DMDAGetGhostCorners(*da,x,&Y,&Z,m,&N,&P);

  if (y) {
    *y = Y;
  }
  if (z) {
    *z = Z;
  }
  if (n) {
    *n = N;
  }
  if (p) {
    *p = P;
  }
}
