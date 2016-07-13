
#include <petsc/private/fortranimpl.h>
#include <petscdmda.h>

#if defined(PETSC_HAVE_FORTRAN_CAPS)
#define dmdasetfieldname_              DMDASETFIELDNAME
#define dmdagetfieldname_              DMDAGETFIELDNAME
#define dmdagetcorners_                DMDAGETCORNERS
#elif !defined(PETSC_HAVE_FORTRAN_UNDERSCORE)
#define dmdasetfieldname_              dmdasetfieldname
#define dmdagetfieldname_              dmdagetfieldname
#define dmdagetcorners_                dmdagetcorners
#endif


PETSC_EXTERN void PETSC_STDCALL dmdasetfieldname_(DM *da,PetscInt *nf,CHAR name PETSC_MIXED_LEN(len),PetscErrorCode *ierr PETSC_END_LEN(len))
{
  char *t;
  FIXCHAR(name,len,t);
  *ierr = DMDASetFieldName(*da,*nf,t);
  FREECHAR(name,t);
}

PETSC_EXTERN void PETSC_STDCALL dmdagetfieldname_(DM *da,PetscInt *nf,CHAR name PETSC_MIXED_LEN(len),PetscErrorCode *ierr PETSC_END_LEN(len))
{
  const char *tname;

  *ierr = DMDAGetFieldName(*da,*nf,&tname);
  *ierr = PetscStrncpy(name,tname,len);
}

PETSC_EXTERN void PETSC_STDCALL  dmdagetcorners_(DM *da,PetscInt *x,PetscInt *y,PetscInt *z,PetscInt *m,PetscInt *n,PetscInt *p, int *ierr )
{
  PetscInt Y,Z,N,P;

  CHKFORTRANNULLINTEGER(y);
  CHKFORTRANNULLINTEGER(z);
  CHKFORTRANNULLINTEGER(n);
  CHKFORTRANNULLINTEGER(p);

  *ierr = DMDAGetCorners(*da,x,&Y,&Z,m,&N,&P);

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
