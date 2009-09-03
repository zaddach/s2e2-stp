/********************************************************************
 * AUTHORS: Vijay Ganesh
 *
 * BEGIN DATE: November, 2005
 *
 * LICENSE: Please view LICENSE file in the home dir of this Program
 ********************************************************************/
// -*- c++ -*-


/* Transform:
 *
 * Converts signed Div/signed remainder/signed modulus into their
 * unsigned counterparts. Removes array selects and stores from
 * formula. Arrays are replaced by equivalent bit-vector variables
 */


#include "AST.h"
#include <stdlib.h>
#include <stdio.h>
namespace BEEV
{

  ASTNode TransformFormula(const ASTNode& form);
  ASTNode TranslateSignedDivModRem(const ASTNode& in);
  ASTNode TransformTerm(const ASTNode& inputterm);
  void assertTransformPostConditions(const ASTNode & term);

  ASTNodeMap* TransformMap;

  const bool debug_transform = false;

  // NB: This is the only function that should be called externally. It sets
  // up the cache that the others use.
  ASTNode BeevMgr::TransformFormula_TopLevel(const ASTNode& form)
  {
    assert(TransformMap == NULL);
    TransformMap = new ASTNodeMap(100);
    ASTNode result = TransformFormula(form);
    if (debug_transform)
      assertTransformPostConditions(result);
    TransformMap->clear();
    delete TransformMap;
    TransformMap = NULL;
    return result;
  }

  //Translates signed BVDIV,BVMOD and BVREM into unsigned variety
  ASTNode TranslateSignedDivModRem(const ASTNode& in)
  {
    BeevMgr& bm = in.GetBeevMgr();
    assert(in.GetChildren().size() ==2);

    ASTNode dividend = in[0];
    ASTNode divisor = in[1];
    unsigned len = in.GetValueWidth();

    ASTNode hi1 = bm.CreateBVConst(32, len - 1);
    ASTNode one = bm.CreateOneConst(1);
    ASTNode zero = bm.CreateZeroConst(1);
    // create the condition for the dividend
    ASTNode cond_dividend = bm.CreateNode(EQ, one, bm.CreateTerm(BVEXTRACT, 1, dividend, hi1, hi1));
    // create the condition for the divisor
    ASTNode cond_divisor = bm.CreateNode(EQ, one, bm.CreateTerm(BVEXTRACT, 1, divisor, hi1, hi1));

    if (SBVREM == in.GetKind())
      {
        //BVMOD is an expensive operation. So have the fewest bvmods possible. Just one.

        //Take absolute value.
        ASTNode pos_dividend = bm.CreateTerm(ITE, len, cond_dividend, bm.CreateTerm(BVUMINUS, len, dividend), dividend);
        ASTNode pos_divisor = bm.CreateTerm(ITE, len, cond_divisor, bm.CreateTerm(BVUMINUS, len, divisor), divisor);

        //create the modulus term
        ASTNode modnode = bm.CreateTerm(BVMOD, len, pos_dividend, pos_divisor);

        //If the dividend is <0 take the unary minus.
        ASTNode n = bm.CreateTerm(ITE, len, cond_dividend, bm.CreateTerm(BVUMINUS, len, modnode), modnode);

        //put everything together, simplify, and return
        return bm.SimplifyTerm_TopLevel(n);
      }

    // This is the modulus of dividing rounding to -infinity.
    // Except if the signs are different, and it perfectly divides
    // the modulus is the divisor (not zero).
    else if (SBVMOD == in.GetKind())
      {
        // (let (?msb_s (extract[|m-1|:|m-1|] s))
        // (let (?msb_t (extract[|m-1|:|m-1|] t))
        // (ite (and (= ?msb_s bit0) (= ?msb_t bit0))
        //      (bvurem s t)
        // (ite (and (= ?msb_s bit1) (= ?msb_t bit0))
        //      (bvadd (bvneg (bvurem (bvneg s) t)) t)
        // (ite (and (= ?msb_s bit0) (= ?msb_t bit1))
        //      (bvadd (bvurem s (bvneg t)) t)
        //      (bvneg (bvurem (bvneg s) (bvneg t)))))))

        //Take absolute value.
        ASTNode pos_dividend = bm.CreateTerm(ITE, len, cond_dividend, bm.CreateTerm(BVUMINUS, len, dividend), dividend);
        ASTNode pos_divisor = bm.CreateTerm(ITE, len, cond_divisor, bm.CreateTerm(BVUMINUS, len, divisor), divisor);

        ASTNode urem_node = bm.CreateTerm(BVMOD, len, pos_dividend, pos_divisor);

        // If the dividend is <0, then we negate the whole thing.
        ASTNode rev_node = bm.CreateTerm(ITE, len, cond_dividend, bm.CreateTerm(BVUMINUS, len, urem_node), urem_node);

        // if It's XOR <0 then add t (not its absolute value).
        ASTNode xor_node = bm.CreateNode(XOR, cond_dividend, cond_divisor);
        ASTNode n = bm.CreateTerm(ITE, len, xor_node, bm.CreateTerm(BVPLUS, len, rev_node, divisor), rev_node);

        return bm.SimplifyTerm_TopLevel(n);
      }
    else if (SBVDIV == in.GetKind())
      {
        //now handle the BVDIV case
        //if topBit(dividend) is 1 and topBit(divisor) is 0
        //
        //then output is -BVDIV(-dividend,divisor)
        //
        //elseif topBit(dividend) is 0 and topBit(divisor) is 1
        //
        //then output is -BVDIV(dividend,-divisor)
        //
        //elseif topBit(dividend) is 1 and topBit(divisor) is 1
        //
        // then output is BVDIV(-dividend,-divisor)
        //
        //else simply output BVDIV(dividend,divisor)

        //Take absolute value.
        ASTNode pos_dividend = bm.CreateTerm(ITE, len, cond_dividend, bm.CreateTerm(BVUMINUS, len, dividend), dividend);
        ASTNode pos_divisor = bm.CreateTerm(ITE, len, cond_divisor, bm.CreateTerm(BVUMINUS, len, divisor), divisor);

        ASTNode divnode = bm.CreateTerm(BVDIV, len, pos_dividend, pos_divisor);

        // A little confusing. Only negate the result if they are XOR <0.
        ASTNode xor_node = bm.CreateNode(XOR, cond_dividend, cond_divisor);
        ASTNode n = bm.CreateTerm(ITE, len, xor_node, bm.CreateTerm(BVUMINUS, len, divnode), divnode);

        return bm.SimplifyTerm_TopLevel(n);
      }

    FatalError("TranslateSignedDivModRem: input must be signed DIV/MOD/REM", in);
    return bm.CreateNode(UNDEFINED);

  }//end of TranslateSignedDivModRem()

  // Check that the transformations have occurred.
  void assertTransformPostConditions(const ASTNode & term)
  {
    const Kind k = term.GetKind();

    // Check the signed operations have been removed.
    assert( SBVDIV != k);
    assert( SBVMOD != k);
    assert( SBVREM !=k);

    // Check the array reads / writes have been removed
    assert( READ !=k );
    assert( WRITE !=k);

    // There should be no nodes left of type array.
    assert(0 == term.GetIndexWidth());

    ASTVec c = term.GetChildren();
    ASTVec::iterator it = c.begin();
    ASTVec::iterator itend = c.end();
    ASTVec o;
    for (; it != itend; it++)
      {
        assertTransformPostConditions(*it);
      }
  }

  ASTNode TransformFormula(const ASTNode& form)
  {
    BeevMgr& bm = form.GetBeevMgr();

    assert(TransformMap != null);

    ASTNode result;

    ASTNode simpleForm = form;
    Kind k = simpleForm.GetKind();
    if (!(is_Form_kind(k) && BOOLEAN_TYPE == simpleForm.GetType()))
      {
        //FIXME: "You have inputted a NON-formula"?
        FatalError("TransformFormula: You have input a NON-formula", simpleForm);
      }

    ASTNodeMap::iterator iter;
    if ((iter = TransformMap->find(simpleForm)) != TransformMap->end())
      return iter->second;

    switch (k)
      {
      case TRUE:
      case FALSE:
        {
          result = simpleForm;
          break;
        }
      case NOT:
        {
          ASTVec c;
          c.push_back(TransformFormula(simpleForm[0]));
          result = bm.CreateNode(NOT, c);
          break;
        }
      case BVLT:
      case BVLE:
      case BVGT:
      case BVGE:
      case BVSLT:
      case BVSLE:
      case BVSGT:
      case BVSGE:
      case NEQ:
        {
          ASTVec c;
          c.push_back(TransformTerm(simpleForm[0]));
          c.push_back(TransformTerm(simpleForm[1]));
          result = bm.CreateNode(k, c);
          break;
        }
      case EQ:
        {
          ASTNode term1 = TransformTerm(simpleForm[0]);
          ASTNode term2 = TransformTerm(simpleForm[1]);
          result = bm.CreateSimplifiedEQ(term1, term2);
          break;
        }
      case AND:
      case OR:
      case NAND:
      case NOR:
      case IFF:
      case XOR:
      case ITE:
      case IMPLIES:
        {
          ASTVec vec;
          ASTNode o;
          for (ASTVec::const_iterator it = simpleForm.begin(), itend = simpleForm.end(); it != itend; it++)
            {
              o = TransformFormula(*it);
              vec.push_back(o);
            }

          result = bm.CreateNode(k, vec);
          break;
        }
      default:
        if (k == SYMBOL && BOOLEAN_TYPE == simpleForm.GetType())
          result = simpleForm;
        else
          {
            cerr << "The input is: " << simpleForm << endl;
            cerr << "The valuewidth of input is : " << simpleForm.GetValueWidth() << endl;
            FatalError("TransformFormula: Illegal kind: ", bm.CreateNode(UNDEFINED), k);
          }
        break;
      }

    if (simpleForm.GetChildren().size() > 0)
      (*TransformMap)[simpleForm] = result;
    return result;
  } //End of TransformFormula

  ASTNode TransformTerm(const ASTNode& inputterm)
  {
    assert(TransformMap != null);

    BeevMgr& bm = inputterm.GetBeevMgr();

    ASTNode result;
    ASTNode term = inputterm;

    Kind k = term.GetKind();
    if (!is_Term_kind(k))
      FatalError("TransformTerm: Illegal kind: You have input a nonterm:", inputterm, k);
    ASTNodeMap::iterator iter;
    if ((iter = TransformMap->find(term)) != TransformMap->end())
      return iter->second;
    switch (k)
      {
      case SYMBOL:
        {
          // ASTNodeMap::iterator itsym;
          //       if((itsym = CounterExampleMap.find(term)) != CounterExampleMap.end())
          //            result = itsym->second;
          //       else
          result = term;
          break;
        }
      case BVCONST:
        result = term;
        break;
      case WRITE:
        FatalError("TransformTerm: this kind is not supported", term);
        break;
      case READ:
        result = bm.TransformArray(term);
        break;
      case ITE:
        {
          ASTNode cond = term[0];
          ASTNode thn = term[1];
          ASTNode els = term[2];
          cond = TransformFormula(cond);
          thn = TransformTerm(thn);
          els = TransformTerm(els);
          //result = CreateTerm(ITE,term.GetValueWidth(),cond,thn,els);
          result = bm.CreateSimplifiedTermITE(cond, thn, els);
          result.SetIndexWidth(term.GetIndexWidth());
          break;
        }
      default:
        {
          ASTVec c = term.GetChildren();
          ASTVec::iterator it = c.begin();
          ASTVec::iterator itend = c.end();
          unsigned width = term.GetValueWidth();
          unsigned indexwidth = term.GetIndexWidth();
          ASTVec o;
          for (; it != itend; it++)
            {
              o.push_back(TransformTerm(*it));
            }

          result = bm.CreateTerm(k, width, o);
          result.SetIndexWidth(indexwidth);

          Kind k = result.GetKind();

          if (BVDIV == k || BVMOD == k || SBVDIV == k || SBVREM == k || SBVMOD == k)
            {
              ASTNode bottom = result[1];

              if (SBVDIV == result.GetKind() || SBVREM == result.GetKind() || SBVMOD == result.GetKind())
                {
                  result = TranslateSignedDivModRem(result);
                }

              if (division_by_zero_returns_one)
                {
                  // This is a difficult rule to introduce in other places because it's recursive. i.e.
                  // result is embedded unchanged inside the result.

                  unsigned inputValueWidth = result.GetValueWidth();
                  ASTNode zero = bm.CreateZeroConst(inputValueWidth);
                  ASTNode one = bm.CreateOneConst(inputValueWidth);
                  result = bm.CreateTerm(ITE, inputValueWidth, bm.CreateNode(EQ, zero, bottom), one, result);
                }
            }
        }
        ////////////////////////////////////////////////////////////////////////////////////


        break;
      }

    if (term.GetChildren().size() > 0)
      (*TransformMap)[term] = result;
    if (term.GetValueWidth() != result.GetValueWidth())
      FatalError("TransformTerm: result and input terms are of different length", result);
    if (term.GetIndexWidth() != result.GetIndexWidth())
      {
        cerr << "TransformTerm: input term is : " << term << endl;
        FatalError("TransformTerm: result and input terms have different index length", result);
      }
    return result;
  } //End of TransformTerm

  /* This function transforms Array Reads, Read over Writes, Read over
   * ITEs into flattened form.
   *
   * Transform1: Suppose there are two array reads in the input
   * Read(A,i) and Read(A,j) over the same array. Then Read(A,i) is
   * replaced with a symbolic constant, say v1, and Read(A,j) is
   * replaced with the following ITE:
   *
   * ITE(i=j,v1,v2)
   *
   */
  ASTNode BeevMgr::TransformArray(const ASTNode& term)
  {
    assert(TransformMap != null);

    ASTNode result = term;

    const unsigned int width = term.GetValueWidth();

    if (READ != term.GetKind())
      FatalError("TransformArray: input term is of wrong kind: ", ASTUndefined);

    ASTNodeMap::iterator iter;
    if ((iter = TransformMap->find(term)) != TransformMap->end())
      return iter->second;

    //'term' is of the form READ(arrName, readIndex)
    const ASTNode & arrName = term[0];
    const ASTNode & readIndex = TransformTerm(term[1]);

    switch (arrName.GetKind())
      {
      case SYMBOL:
        {
          /* input is of the form: READ(A, readIndex)
           *
           * output is of the from: A1, if this is the first READ over A
           *
           *                        ITE(previous_readIndex=readIndex,A1,A2)
           *
           *                        .....
           */

          //  Recursively transform read index, which may also contain reads.
          ASTNode processedTerm = CreateTerm(READ, width, arrName, readIndex);

          //check if the 'processedTerm' has a corresponding ITE construct
          //already. if so, return it. else continue processing.
          ASTNodeMap::iterator it;
          if ((it = _arrayread_ite.find(processedTerm)) != _arrayread_ite.end())
            {
              result = it->second;
              break;
            }
          //Constructing Symbolic variable corresponding to 'processedTerm'
          ASTNode CurrentSymbol;
          ASTNodeMap::iterator it1;
          // First, check if read index is constant and it has a constant value in the substitution map.
          if (CheckSubstitutionMap(processedTerm, CurrentSymbol))
            {
              _arrayread_symbol[processedTerm] = CurrentSymbol;
            }
          // Check if it already has an abstract variable.
          else if ((it1 = _arrayread_symbol.find(processedTerm)) != _arrayread_symbol.end())
            {
              CurrentSymbol = it1->second;
            }
          else
            {
              // Make up a new abstract variable.
              // FIXME: Make this into a method (there already may BE a method) and
              // get rid of the fixed-length buffer!
              //build symbolic name corresponding to array read. The symbolic
              //name has 2 components: stringname, and a count
              const char * b = arrName.GetName();
              std::string c(b);
              char d[32];
              sprintf(d, "%d", _symbol_count++);
              std::string ccc(d);
              c += "array_" + ccc;

              CurrentSymbol = CreateSymbol(c.c_str());
              CurrentSymbol.SetValueWidth(processedTerm.GetValueWidth());
              CurrentSymbol.SetIndexWidth(processedTerm.GetIndexWidth());
              _arrayread_symbol[processedTerm] = CurrentSymbol;
            }

          //list of array-read indices corresponding to arrName, seen while
          //traversing the AST tree. we need this list to construct the ITEs
          // Dill: we hope to make this irrelevant.  Harmless for now.
          const ASTVec & readIndices = _arrayname_readindices[arrName];

          //construct the ITE structure for this array-read
          ASTNode ite = CurrentSymbol;
          _introduced_symbols.insert(CurrentSymbol);
          assert(BVTypeCheck(ite));

          if (arrayread_refinement_flag)
            {
              // ite is really a variable here; it is an ite in the
              // else-branch
              result = ite;
            }
          else
            {
              // Full Array transform if we're not doing read refinement.
              ASTVec::const_reverse_iterator it2 = readIndices.rbegin();
              ASTVec::const_reverse_iterator it2end = readIndices.rend();
              for (; it2 != it2end; it2++)
                {
                  ASTNode cond = CreateSimplifiedEQ(readIndex, *it2);
                  if (ASTFalse == cond)
                    continue;

                  ASTNode arrRead = CreateTerm(READ, width, arrName, *it2);
                  assert(BVTypeCheck(arrRead));

                  ASTNode arrayreadSymbol = _arrayread_symbol[arrRead];
                  if (arrayreadSymbol.IsNull())
                    FatalError("TransformArray:symbolic variable for processedTerm, p,"
                               "does not exist:p = ", arrRead);
                  ite = CreateSimplifiedTermITE(cond, arrayreadSymbol, ite);
                }
              result = ite;
              //}
            }

          _arrayname_readindices[arrName].push_back(readIndex);
          //save the ite corresponding to 'processedTerm'
          _arrayread_ite[processedTerm] = result;
          break;
        } //end of READ over a SYMBOL
      case WRITE:
        {
          /* The input to this case is: READ((WRITE A i val) j)
           *
           * The output of this case is: ITE( (= i j) val (READ A i))
           */

          /* 1. arrName or term[0] is infact a WRITE(A,i,val) expression
           *
           * 2. term[1] is the read-index j
           *
           * 3. arrName[0] is the new arrName i.e. A. A can be either a
           SYMBOL or a nested WRITE. no other possibility
           *
           * 4. arrName[1] is the WRITE index i.e. i
           *
           * 5. arrName[2] is the WRITE value i.e. val (val can inturn
           *    be an array read)
           */

          ASTNode writeIndex = TransformTerm(arrName[1]);
          ASTNode writeVal = TransformTerm(arrName[2]);

          if (ARRAY_TYPE != arrName[0].GetType())
            FatalError("TransformArray: An array write is being attempted on a non-array:", term);

          if ((SYMBOL == arrName[0].GetKind() || WRITE == arrName[0].GetKind()))
            {
              ASTNode cond = CreateSimplifiedEQ(writeIndex, readIndex);
              BVTypeCheck(cond);

              ASTNode readTerm = CreateTerm(READ, width, arrName[0], readIndex);
              BVTypeCheck(readTerm);

              ASTNode readPushedIn = TransformArray(readTerm);
              BVTypeCheck(readPushedIn);

              result = CreateSimplifiedTermITE(cond, writeVal, readPushedIn);

              BVTypeCheck(result);
            }
          else if (ITE == arrName[0].GetKind())
            {
              // pull out the ite from the write // pushes the write through.
              ASTNode writeTrue = CreateNode(WRITE, (arrName[0][1]), writeIndex, writeVal);
              writeTrue.SetIndexWidth(writeIndex.GetValueWidth());
              writeTrue.SetValueWidth(writeVal.GetValueWidth());
              assert(ARRAY_TYPE == writeTrue.GetType());

              ASTNode writeFalse = CreateNode(WRITE, (arrName[0][2]), writeIndex, writeVal);
              writeFalse.SetIndexWidth(writeIndex.GetValueWidth());
              writeFalse.SetValueWidth(writeVal.GetValueWidth());
              assert(ARRAY_TYPE == writeFalse.GetType());

              result = CreateSimplifiedTermITE(TransformFormula(arrName[0][0]), writeTrue, writeFalse);
              result.SetIndexWidth(writeIndex.GetValueWidth());
              result.SetValueWidth(writeVal.GetValueWidth());
              assert(ARRAY_TYPE == result.GetType());

              result = CreateTerm(READ, writeVal.GetValueWidth(), result, readIndex);
              BVTypeCheck(result);
              result = TransformArray(result);
            }
          else
            FatalError("TransformArray: Write over bad type.");
          break;
        } //end of READ over a WRITE
      case ITE:
        {
          /* READ((ITE cond thn els) j)
           *
           * is transformed into
           *
           * (ITE cond (READ thn j) (READ els j))
           */

          // pull out the ite from the read // pushes the read through.

          //(ITE cond thn els)

          ASTNode cond = arrName[0];
          cond = TransformFormula(cond);

          const ASTNode& thn = arrName[1];
          const ASTNode& els = arrName[2];

          //(READ thn j)
          ASTNode thnRead = CreateTerm(READ, width, thn, readIndex);
          BVTypeCheck(thnRead);
          thnRead = TransformArray(thnRead);

          //(READ els j)
          ASTNode elsRead = CreateTerm(READ, width, els, readIndex);
          BVTypeCheck(elsRead);
          elsRead = TransformArray(elsRead);

          //(ITE cond (READ thn j) (READ els j))
          result = CreateSimplifiedTermITE(cond, thnRead, elsRead);
          BVTypeCheck(result);
          break;
        }
      default:
        FatalError("TransformArray: The READ is NOT over SYMBOL/WRITE/ITE", term);
        break;
      }

    (*TransformMap)[term] = result;
    return result;
  } //end of TransformArray()

  ASTNode BeevMgr::TransformFiniteFor(const ASTNode& form)
  {
    return form;
  }

} //end of namespace BEEV
