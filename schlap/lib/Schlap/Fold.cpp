#include <schlap/Dialect.h>
#include <schlap/Value.h>
#include <mlir/IR/Builders.h>
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/TypeSwitch.h>
#include <llvm/Support/MathExtras.h>
#include <algorithm>

using mlir::ArrayAttr;
using mlir::Attribute;
using mlir::OpFoldResult;

std::optional<int64_t> schlap::getIntValue(Attribute Value) {
  auto Int = dyn_cast_if_present<mlir::IntegerAttr>(Value);
  if (!Int || !Int.getType().isSignlessInteger(64))
    return std::nullopt;
  return Int.getInt();
}

Attribute schlap::getIntValue(mlir::MLIRContext* Context, int64_t N) {
  return mlir::IntegerAttr::get(mlir::IntegerType::get(Context, 64), N);
}

// Rank the kinds of values for compareValues.
static int getKindRank(Attribute A) {
  if (isa<mlir::BoolAttr>(A))
    return 0;
  if (isa<mlir::IntegerAttr>(A))
    return 1;
  if (isa<mlir::StringAttr>(A))
    return 2;
  if (isa<ArrayAttr>(A))
    return 3;
  return 4;
}

int schlap::compareValues(Attribute A, Attribute B) {
  if (A == B)
    return 0;
  int RankA = getKindRank(A);
  int RankB = getKindRank(B);
  if (RankA != RankB)
    return RankA < RankB ? -1 : 1;

  switch (RankA) {
  case 0:
    return cast<mlir::BoolAttr>(A).getValue() ? 1 : -1;
  case 1: {
    llvm::APInt X = cast<mlir::IntegerAttr>(A).getValue();
    llvm::APInt Y = cast<mlir::IntegerAttr>(B).getValue();
    if (X.getBitWidth() != Y.getBitWidth())
      return X.getBitWidth() < Y.getBitWidth() ? -1 : 1;
    if (X == Y)
      return 0;
    return X.slt(Y) ? -1 : 1;
  }
  case 2:
    return cast<mlir::StringAttr>(A).getValue().compare(
        cast<mlir::StringAttr>(B).getValue());
  case 3: {
    auto X = cast<ArrayAttr>(A).getValue();
    auto Y = cast<ArrayAttr>(B).getValue();
    for (auto [XE, YE] : llvm::zip(X, Y))
      if (int C = compareValues(XE, YE))
        return C;
    if (X.size() == Y.size())
      return 0;
    return X.size() < Y.size() ? -1 : 1;
  }
  default: {
    std::string X, Y;
    llvm::raw_string_ostream XOS(X), YOS(Y);
    A.print(XOS);
    B.print(YOS);
    return llvm::StringRef(X).compare(Y);
  }
  }
}

ArrayAttr schlap::getSetValue(mlir::MLIRContext* Context,
                              llvm::SmallVectorImpl<Attribute>& Elements) {
  llvm::sort(Elements, [](Attribute A, Attribute B) {
    return compareValues(A, B) < 0;
  });
  Elements.erase(std::unique(Elements.begin(), Elements.end()),
                 Elements.end());
  return ArrayAttr::get(Context, Elements);
}

Attribute schlap::findInSet(ArrayAttr Set, Attribute Element) {
  auto Elements = Set.getValue();
  auto It = llvm::partition_point(Elements, [&](Attribute A) {
    return compareValues(A, Element) < 0;
  });
  if (It == Elements.end() || *It != Element)
    return nullptr;
  return *It;
}

ArrayAttr schlap::findInFunction(ArrayAttr Fn, Attribute Arg) {
  auto Pairs = Fn.getValue();
  auto It = llvm::partition_point(Pairs, [&](Attribute Pair) {
    return compareValues(cast<ArrayAttr>(Pair)[0], Arg) < 0;
  });
  if (It == Pairs.end() || cast<ArrayAttr>(*It)[0] != Arg)
    return nullptr;
  return cast<ArrayAttr>(*It);
}

// Print the elements of an array value between Open and Close.
static void printElements(llvm::raw_ostream& OS, ArrayAttr Array,
                          llvm::function_ref<mlir::Type(size_t)> GetType,
                          llvm::StringRef Open, llvm::StringRef Close) {
  OS << Open;
  for (auto [I, Element] : llvm::enumerate(Array.getValue())) {
    if (I != 0)
      OS << ", ";
    schlap::printValue(OS, Element, GetType(I));
  }
  OS << Close;
}

void schlap::printValue(llvm::raw_ostream& OS, Attribute Value,
                        mlir::Type Type) {
  mlir::Type Unknown;
  if (auto Bool = dyn_cast<mlir::BoolAttr>(Value)) {
    OS << (Bool.getValue() ? "⊤" : "⊥");
  } else if (std::optional<int64_t> N = getIntValue(Value)) {
    OS << *N;
  } else if (auto Atom = dyn_cast<mlir::StringAttr>(Value)) {
    OS << Atom.getValue();
  } else if (auto Array = dyn_cast<ArrayAttr>(Value)) {
    if (auto TT = dyn_cast_if_present<TupleType>(Type)) {
      printElements(OS, Array, [&](size_t I) {
        return I < TT.getElementTypes().size() ? TT.getElementTypes()[I]
                                               : Unknown;
      }, "⟨", "⟩");
    } else if (auto FT = dyn_cast_if_present<FnType>(Type)) {
      OS << '[';
      for (auto [I, Pair] : llvm::enumerate(Array.getValue())) {
        auto KV = cast<ArrayAttr>(Pair);
        if (I != 0)
          OS << ", ";
        printValue(OS, KV[0], FT.getDomainType());
        OS << " ↦ ";
        printValue(OS, KV[1], FT.getRangeType());
      }
      OS << ']';
    } else {
      auto ST = dyn_cast_if_present<SetType>(Type);
      mlir::Type ElementType = ST ? ST.getElementType() : Unknown;
      printElements(OS, Array, [&](size_t) { return ElementType; }, "{", "}");
    }
  } else {
    if (isa<mlir::TypedAttr>(Value))
      Value.print(OS, /*elideType=*/true);
    else
      Value.print(OS);
  }
}

// Return the truth value of an attribute.
static std::optional<bool> getBool(Attribute A) {
  if (auto Bool = dyn_cast_if_present<mlir::BoolAttr>(A))
    return Bool.getValue();
  return std::nullopt;
}

// Return the set of an attribute.
static ArrayAttr getSet(Attribute A) {
  return dyn_cast_if_present<ArrayAttr>(A);
}

// Return V when it can replace the result of Op.
static OpFoldResult forward(mlir::Operation* Op, mlir::Value V) {
  if (V.getType() != Op->getResult(0).getType())
    return {};
  return V;
}

static Attribute getBoolAttr(mlir::Operation* Op, bool B) {
  return mlir::BoolAttr::get(Op->getContext(), B);
}

// Formulas

OpFoldResult schlap::TrueOp::fold(FoldAdaptor) {
  return getBoolAttr(*this, true);
}

OpFoldResult schlap::FalseOp::fold(FoldAdaptor) {
  return getBoolAttr(*this, false);
}

OpFoldResult schlap::AndOp::fold(FoldAdaptor Adaptor) {
  std::optional<bool> L = getBool(Adaptor.getLhs());
  std::optional<bool> R = getBool(Adaptor.getRhs());
  if (L == false || R == false)
    return getBoolAttr(*this, false);
  if (L == true)
    return R ? OpFoldResult(Adaptor.getRhs()) : forward(*this, getRhs());
  if (R == true)
    return forward(*this, getLhs());
  return {};
}

OpFoldResult schlap::OrOp::fold(FoldAdaptor Adaptor) {
  std::optional<bool> L = getBool(Adaptor.getLhs());
  std::optional<bool> R = getBool(Adaptor.getRhs());
  if (L == true || R == true)
    return getBoolAttr(*this, true);
  if (L == false)
    return R ? OpFoldResult(Adaptor.getRhs()) : forward(*this, getRhs());
  if (R == false)
    return forward(*this, getLhs());
  return {};
}

OpFoldResult schlap::NotOp::fold(FoldAdaptor Adaptor) {
  if (std::optional<bool> A = getBool(Adaptor.getArg()))
    return getBoolAttr(*this, !*A);
  return {};
}

OpFoldResult schlap::ImpliesOp::fold(FoldAdaptor Adaptor) {
  std::optional<bool> L = getBool(Adaptor.getLhs());
  std::optional<bool> R = getBool(Adaptor.getRhs());
  if (L == false || R == true)
    return getBoolAttr(*this, true);
  if (L == true)
    return R ? OpFoldResult(Adaptor.getRhs()) : forward(*this, getRhs());
  return {};
}

OpFoldResult schlap::IffOp::fold(FoldAdaptor Adaptor) {
  std::optional<bool> L = getBool(Adaptor.getLhs());
  std::optional<bool> R = getBool(Adaptor.getRhs());
  if (L && R)
    return getBoolAttr(*this, *L == *R);
  return {};
}

OpFoldResult schlap::EqOp::fold(FoldAdaptor Adaptor) {
  if (getLhs() == getRhs())
    return getBoolAttr(*this, true);
  if (!Adaptor.getLhs() || !Adaptor.getRhs())
    return {};
  return getBoolAttr(*this, Adaptor.getLhs() == Adaptor.getRhs());
}

OpFoldResult schlap::NeOp::fold(FoldAdaptor Adaptor) {
  if (getLhs() == getRhs())
    return getBoolAttr(*this, false);
  if (!Adaptor.getLhs() || !Adaptor.getRhs())
    return {};
  return getBoolAttr(*this, Adaptor.getLhs() != Adaptor.getRhs());
}

// Integers

OpFoldResult schlap::IntOp::fold(FoldAdaptor) {
  return getValueAttr();
}

// Fold an integer operation with F returning true on overflow.
template <typename Fn>
static OpFoldResult foldInt(mlir::Operation* Op, Attribute A, Attribute B,
                            Fn F) {
  std::optional<int64_t> X = schlap::getIntValue(A);
  std::optional<int64_t> Y = schlap::getIntValue(B);
  if (!X || !Y)
    return {};
  int64_t Result;
  if (F(*X, *Y, Result))
    return {};
  return schlap::getIntValue(Op->getContext(), Result);
}

OpFoldResult schlap::AddOp::fold(FoldAdaptor Adaptor) {
  return foldInt(*this, Adaptor.getLhs(), Adaptor.getRhs(),
                 [](int64_t X, int64_t Y, int64_t& R) {
                   return llvm::AddOverflow(X, Y, R);
                 });
}

OpFoldResult schlap::SubOp::fold(FoldAdaptor Adaptor) {
  return foldInt(*this, Adaptor.getLhs(), Adaptor.getRhs(),
                 [](int64_t X, int64_t Y, int64_t& R) {
                   return llvm::SubOverflow(X, Y, R);
                 });
}

OpFoldResult schlap::MulOp::fold(FoldAdaptor Adaptor) {
  return foldInt(*this, Adaptor.getLhs(), Adaptor.getRhs(),
                 [](int64_t X, int64_t Y, int64_t& R) {
                   return llvm::MulOverflow(X, Y, R);
                 });
}

OpFoldResult schlap::NegOp::fold(FoldAdaptor Adaptor) {
  return foldInt(*this, getIntValue(getContext(), 0), Adaptor.getArg(),
                 [](int64_t X, int64_t Y, int64_t& R) {
                   return llvm::SubOverflow(X, Y, R);
                 });
}

// Fold a comparison of two integers with F.
template <typename Fn>
static OpFoldResult foldCompare(mlir::Operation* Op, Attribute A,
                                Attribute B, Fn F) {
  std::optional<int64_t> X = schlap::getIntValue(A);
  std::optional<int64_t> Y = schlap::getIntValue(B);
  if (!X || !Y)
    return {};
  return getBoolAttr(Op, F(*X, *Y));
}

OpFoldResult schlap::LtOp::fold(FoldAdaptor Adaptor) {
  return foldCompare(*this, Adaptor.getLhs(), Adaptor.getRhs(),
                     [](int64_t X, int64_t Y) { return X < Y; });
}

OpFoldResult schlap::LeOp::fold(FoldAdaptor Adaptor) {
  return foldCompare(*this, Adaptor.getLhs(), Adaptor.getRhs(),
                     [](int64_t X, int64_t Y) { return X <= Y; });
}

OpFoldResult schlap::GtOp::fold(FoldAdaptor Adaptor) {
  return foldCompare(*this, Adaptor.getLhs(), Adaptor.getRhs(),
                     [](int64_t X, int64_t Y) { return X > Y; });
}

OpFoldResult schlap::GeOp::fold(FoldAdaptor Adaptor) {
  return foldCompare(*this, Adaptor.getLhs(), Adaptor.getRhs(),
                     [](int64_t X, int64_t Y) { return X >= Y; });
}

// Sets

OpFoldResult schlap::EmptySetOp::fold(FoldAdaptor) {
  return ArrayAttr::get(getContext(), {});
}

OpFoldResult schlap::MemberOp::fold(FoldAdaptor Adaptor) {
  ArrayAttr Set = getSet(Adaptor.getRhs());
  if (!Set || !Adaptor.getLhs())
    return {};
  return getBoolAttr(*this, findInSet(Set, Adaptor.getLhs()) != nullptr);
}

OpFoldResult schlap::NotMemberOp::fold(FoldAdaptor Adaptor) {
  ArrayAttr Set = getSet(Adaptor.getRhs());
  if (!Set || !Adaptor.getLhs())
    return {};
  return getBoolAttr(*this, findInSet(Set, Adaptor.getLhs()) == nullptr);
}

OpFoldResult schlap::UnionOp::fold(FoldAdaptor Adaptor) {
  ArrayAttr A = getSet(Adaptor.getLhs());
  ArrayAttr B = getSet(Adaptor.getRhs());
  if (!A || !B)
    return {};
  llvm::SmallVector<Attribute> Elements(A.getValue());
  llvm::append_range(Elements, B.getValue());
  return getSetValue(getContext(), Elements);
}

OpFoldResult schlap::IntersectOp::fold(FoldAdaptor Adaptor) {
  ArrayAttr A = getSet(Adaptor.getLhs());
  ArrayAttr B = getSet(Adaptor.getRhs());
  if (!A || !B)
    return {};
  llvm::SmallVector<Attribute> Elements;
  for (Attribute E : A.getValue())
    if (findInSet(B, E))
      Elements.push_back(E);
  return ArrayAttr::get(getContext(), Elements);
}

OpFoldResult schlap::SubsetOp::fold(FoldAdaptor Adaptor) {
  ArrayAttr A = getSet(Adaptor.getLhs());
  ArrayAttr B = getSet(Adaptor.getRhs());
  if (!A || !B)
    return {};
  return getBoolAttr(*this, llvm::all_of(A.getValue(), [&](Attribute E) {
                       return findInSet(B, E) != nullptr;
                     }));
}

// Do not fold a range of more elements than this.
static constexpr int64_t MaxRangeSize = int64_t(1) << 20;

OpFoldResult schlap::RangeOp::fold(FoldAdaptor Adaptor) {
  std::optional<int64_t> Lo = getIntValue(Adaptor.getLhs());
  std::optional<int64_t> Hi = getIntValue(Adaptor.getRhs());
  if (!Lo || !Hi)
    return {};
  llvm::SmallVector<Attribute> Elements;
  if (*Lo <= *Hi) {
    int64_t Size;
    if (llvm::SubOverflow(*Hi, *Lo, Size) || Size >= MaxRangeSize)
      return {};
    for (int64_t I = 0; I <= Size; ++I)
      Elements.push_back(getIntValue(getContext(), *Lo + I));
  }
  return ArrayAttr::get(getContext(), Elements);
}

OpFoldResult schlap::SetOfOp::fold(FoldAdaptor Adaptor) {
  if (llvm::is_contained(Adaptor.getElements(), Attribute()))
    return {};
  llvm::SmallVector<Attribute> Elements(Adaptor.getElements());
  return getSetValue(getContext(), Elements);
}

// Values

OpFoldResult schlap::IteOp::fold(FoldAdaptor Adaptor) {
  std::optional<bool> C = getBool(Adaptor.getCond());
  if (!C)
    return {};
  Attribute A = *C ? Adaptor.getLhs() : Adaptor.getRhs();
  if (A)
    return A;
  return forward(*this, *C ? getLhs() : getRhs());
}

OpFoldResult schlap::AtomOp::fold(FoldAdaptor) {
  return getNameAttr();
}

OpFoldResult schlap::TupleOp::fold(FoldAdaptor Adaptor) {
  if (llvm::is_contained(Adaptor.getElements(), Attribute()))
    return {};
  return ArrayAttr::get(getContext(), Adaptor.getElements());
}

OpFoldResult schlap::TupleGetOp::fold(FoldAdaptor Adaptor) {
  auto Tuple = dyn_cast_if_present<ArrayAttr>(Adaptor.getTuple());
  uint64_t Index = getIndex();
  if (!Tuple || Index >= Tuple.size())
    return {};
  return Tuple[Index];
}

OpFoldResult schlap::ApplyOp::fold(FoldAdaptor Adaptor) {
  auto Fn = dyn_cast_if_present<ArrayAttr>(Adaptor.getLhs());
  if (!Fn || !Adaptor.getRhs())
    return {};
  if (ArrayAttr Pair = findInFunction(Fn, Adaptor.getRhs()))
    return Pair[1];
  return {};
}

OpFoldResult schlap::UpdateOp::fold(FoldAdaptor Adaptor) {
  auto Fn = dyn_cast_if_present<ArrayAttr>(Adaptor.getFn());
  if (!Fn || !Adaptor.getArg() || !Adaptor.getValue())
    return {};
  ArrayAttr Pair = findInFunction(Fn, Adaptor.getArg());
  if (!Pair)
    return {};
  llvm::SmallVector<Attribute> Pairs(Fn.getValue());
  for (Attribute& P : Pairs)
    if (P == Pair)
      P = ArrayAttr::get(getContext(), {Adaptor.getArg(), Adaptor.getValue()});
  return ArrayAttr::get(getContext(), Pairs);
}

mlir::Operation* schlap::SchlapDialect::materializeConstant(
    mlir::OpBuilder& Builder, Attribute Value, mlir::Type Type,
    mlir::Location Loc) {
  if (isa<IntType>(Type) && getIntValue(Value))
    return IntOp::create(Builder, Loc, Type, cast<mlir::IntegerAttr>(Value));
  if (isa<PropType>(Type)) {
    if (std::optional<bool> B = getBool(Value))
      return *B ? TrueOp::create(Builder, Loc, Type).getOperation()
                : FalseOp::create(Builder, Loc, Type).getOperation();
  }
  if (isa<SetType>(Type))
    if (auto Set = dyn_cast<ArrayAttr>(Value); Set && Set.empty())
      return EmptySetOp::create(Builder, Loc, Type);
  if (isa<AtomType>(Type))
    if (auto Name = dyn_cast<mlir::StringAttr>(Value))
      return AtomOp::create(Builder, Loc, Type, Name);
  return nullptr;
}
