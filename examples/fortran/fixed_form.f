C MPF fixed-form source and continuation example
      PROGRAM FIXEDFORM
      INTEGER VALUES(4)
      INTEGER TOTAL
      VALUES = (/1, 2,
     & 3, 4/)
      TOTAL = VALUES(1) + VALUES(2) +
     &        VALUES(3) + VALUES(4)
      TOTAL = TOTAL + 32
      PRINT *, TOTAL
      END
