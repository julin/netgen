#ifndef NETGEN_CORE_ARCHIVE_HPP
#define NETGEN_CORE_ARCHIVE_HPP

#include <complex>              // for complex
#include <cstring>              // for size_t, strlen
#include <fstream>              // for operator<<, ifstream, ofstream, basic...
#include <functional>           // for function
#include <map>                  // for map, _Rb_tree_iterator
#include <memory>               // for __shared_ptr_access, __shared_ptr_acc...
#include <stdexcept>            // for runtime_error
#include <string>               // for string, operator+
#include <type_traits>          // for declval, enable_if, false_type, is_co...
#include <typeinfo>             // for type_info
#include <utility>              // for move, swap, pair
#include <vector>               // for vector

#include "ngcore_api.hpp"       // for NGCORE_API, unlikely
#include "type_traits.hpp"      // for all_of_tmpl
#include "version.hpp"          // for VersionInfo

namespace ngcore
{
  // Libraries using this archive can store their version here to implement backwards compatibility
  NGCORE_API const VersionInfo& GetLibraryVersion(const std::string& library);
  NGCORE_API void SetLibraryVersion(const std::string& library, const VersionInfo& version);
  NGCORE_API std::string Demangle(const char* typeinfo);

  class NGCORE_API Archive;

  namespace detail
  {
    // create new pointer of type T if it is default constructible, else throw
    template<typename T, typename ...Rest>
    T* constructIfPossible_impl(Rest... /*unused*/)
    { throw std::runtime_error(std::string(Demangle(typeid(T).name())) + " is not default constructible!"); }

    template<typename T, typename= typename std::enable_if<std::is_constructible<T>::value>::type>
    T* constructIfPossible_impl(int /*unused*/) { return new T; } // NOLINT

    template<typename T>
    T* constructIfPossible() { return constructIfPossible_impl<T>(int{}); }

    //Type trait to check if a class implements a 'void DoArchive(Archive&)' function
    template<typename T>
    struct has_DoArchive
    {
    private:
      template<typename T2>
      static constexpr auto check(T2*) ->
        typename std::is_same<decltype(std::declval<T2>().DoArchive(std::declval<Archive&>())),void>::type;
      template<typename>
      static constexpr std::false_type check(...);
      using type = decltype(check<T>(nullptr)); // NOLINT
    public:
      NGCORE_API static constexpr bool value = type::value;
    };

    // Check if class is archivable
    template<typename T>
    struct is_Archivable_struct
    {
    private:
      template<typename T2>
      static constexpr auto check(T2*) ->
        typename std::is_same<decltype(std::declval<Archive>() & std::declval<T2&>()),Archive&>::type;
      template<typename>
      static constexpr std::false_type check(...);
      using type = decltype(check<T>(nullptr)); // NOLINT
    public:
      NGCORE_API static constexpr bool value = type::value;
    };

    struct ClassArchiveInfo
    {
      // create new object of this type and return a void* pointer that is points to the location
      // of the (base)class given by type_info
      std::function<void*(const std::type_info&)> creator;
      // This caster takes a void* pointer to the type stored in this info and casts it to a
      // void* pointer pointing to the (base)class type_info
      std::function<void*(const std::type_info&, void*)> upcaster;
      // This caster takes a void* pointer to the (base)class type_info and returns void* pointing
      // to the type stored in this info
      std::function<void*(const std::type_info&, void*)> downcaster;
    };
  } // namespace detail

  template<typename T>
  constexpr bool is_archivable = detail::is_Archivable_struct<T>::value;

  // Base Archive class
  class NGCORE_API Archive
  {
    const bool is_output;
    // how many different shared_ptr/pointer have been (un)archived
    int shared_ptr_count, ptr_count;
    // maps for archived shared pointers and pointers
    std::map<void*, int> shared_ptr2nr, ptr2nr;
    // vectors for storing the unarchived (shared) pointers
    std::vector<std::shared_ptr<void>> nr2shared_ptr;
    std::vector<void*> nr2ptr;

  public:
    Archive() = delete;
    Archive(const Archive&) = delete;
    Archive(Archive&&) = delete;
    Archive (bool ais_output) :
      is_output(ais_output), shared_ptr_count(0), ptr_count(0) { ; }

    virtual ~Archive() { ; }

    Archive& operator=(const Archive&) = delete;
    Archive& operator=(Archive&&) = delete;

    bool Output () const { return is_output; }
    bool Input () const { return !is_output; }
    virtual const VersionInfo& GetVersion(const std::string& library)
    { return GetLibraryVersions()[library]; }

    // Pure virtual functions that have to be implemented by In-/OutArchive
    virtual Archive & operator & (double & d) = 0;
    virtual Archive & operator & (int & i) = 0;
    virtual Archive & operator & (long & i) = 0;
    virtual Archive & operator & (size_t & i) = 0;
    virtual Archive & operator & (short & i) = 0;
    virtual Archive & operator & (unsigned char & i) = 0;
    virtual Archive & operator & (bool & b) = 0;
    virtual Archive & operator & (std::string & str) = 0;
    virtual Archive & operator & (char *& str) = 0;

    virtual Archive & operator & (VersionInfo & version)
    {
        if(Output())
            (*this) << version.to_string();
        else
        {
            std::string s;
            (*this) & s;
            version = VersionInfo(s);
        }
        return *this;
    }

    // Archive std classes ================================================
    template<typename T>
    Archive& operator & (std::complex<T>& c)
    {
      if(Output())
          (*this) << c.real() << c.imag();
      else
        {
          T tmp;
          (*this) & tmp;
          c.real(tmp);
          (*this) & tmp;
          c.imag(tmp);
        }
      return (*this);
    }
    template<typename T>
    Archive& operator & (std::vector<T>& v)
    {
      size_t size;
      if(Output())
          size = v.size();
      (*this) & size;
      if(Input())
        v.resize(size);
      Do(&v[0], size);
      return (*this);
    }
    template<typename T1, typename T2>
    Archive& operator& (std::map<T1, T2>& map)
    {
      if(Output())
        {
          (*this) << size_t(map.size());
          for(auto& pair : map)
              (*this) << pair.first << pair.second;
        }
      else
        {
          size_t size = 0;
          (*this) & size;
          T1 key; T2 val;
          for(size_t i = 0; i < size; i++)
            {
              T1 key; T2 val;
              (*this) & key & val;
              map[key] = val;
            }
        }
      return (*this);
    }
    // Archive arrays =====================================================
    // this functions can be overloaded in Archive implementations for more efficiency
    template <typename T, typename = typename std::enable_if<is_archivable<T>>::type>
    Archive & Do (T * data, size_t n)
    { for (size_t j = 0; j < n; j++) { (*this) & data[j]; }; return *this; }; // NOLINT

    virtual Archive & Do (double * d, size_t n)
    { for (size_t j = 0; j < n; j++) { (*this) & d[j]; }; return *this; }; // NOLINT

    virtual Archive & Do (int * i, size_t n)
    { for (size_t j = 0; j < n; j++) { (*this) & i[j]; }; return *this; }; // NOLINT

    virtual Archive & Do (long * i, size_t n)
    { for (size_t j = 0; j < n; j++) { (*this) & i[j]; }; return *this; }; // NOLINT

    virtual Archive & Do (size_t * i, size_t n)
    { for (size_t j = 0; j < n; j++) { (*this) & i[j]; }; return *this; }; // NOLINT

    virtual Archive & Do (short * i, size_t n)
    { for (size_t j = 0; j < n; j++) { (*this) & i[j]; }; return *this; }; // NOLINT

    virtual Archive & Do (unsigned char * i, size_t n)
    { for (size_t j = 0; j < n; j++) { (*this) & i[j]; }; return *this; }; // NOLINT

    virtual Archive & Do (bool * b, size_t n)
    { for (size_t j = 0; j < n; j++) { (*this) & b[j]; }; return *this; }; // NOLINT

    // Archive a class implementing a (void DoArchive(Archive&)) method =======
    template<typename T, typename=std::enable_if_t<detail::has_DoArchive<T>::value>>
    Archive& operator & (T& val)
    {
      val.DoArchive(*this); return *this;
    }

    // Archive shared_ptrs =================================================
    template <typename T>
    Archive& operator & (std::shared_ptr<T>& ptr)
    {
      if(Output())
        {
          // save -2 for nullptr
          if(!ptr)
            return (*this) << -2;

          void* reg_ptr = ptr.get();
          bool neededDowncast = false;
          // Downcasting is only possible for our registered classes
          if(typeid(T) != typeid(*ptr))
            {
              if(!IsRegistered(Demangle(typeid(*ptr).name())))
                  throw std::runtime_error(std::string("Archive error: Polymorphic type ")
                                           + Demangle(typeid(*ptr).name())
                                           + " not registered for archive");
              reg_ptr = GetArchiveRegister(Demangle(typeid(*ptr).name())).downcaster(typeid(T), ptr.get());
              // if there was a true downcast we have to store more information
              if(reg_ptr != static_cast<void*>(ptr.get()) )
                  neededDowncast = true;
            }
          auto pos = shared_ptr2nr.find(reg_ptr);
          // if not found store -1 and the pointer
          if(pos == shared_ptr2nr.end())
            {
              auto p = ptr.get();
              (*this) << -1;
              (*this) & neededDowncast & p;
              // if we did downcast we store the true type as well
              if(neededDowncast)
                (*this) << Demangle(typeid(*ptr).name());
              shared_ptr2nr[reg_ptr] = shared_ptr_count++;
              return *this;
            }
          // if found store the position and if it has to be downcasted and how
          (*this) << pos->second << neededDowncast;
          if(neededDowncast)
            (*this) << Demangle(typeid(*ptr).name());
        }
      else // Input
        {
          int nr;
          (*this) & nr;
          // -2 restores a nullptr
          if(nr == -2)
            {
              ptr = nullptr;
              return *this;
            }
          // -1 restores a new shared ptr by restoring the inner pointer and creating a shared_ptr to it
          if (nr == -1)
            {
              T* p = nullptr;
              bool neededDowncast;
              (*this) & neededDowncast & p;
              ptr = std::shared_ptr<T>(p);
              // if we did downcast we need to store a shared_ptr<void> to the true object
              if(neededDowncast)
                {
                  std::string name;
                  (*this) & name;
                  auto info = GetArchiveRegister(name);
                  // for this we use an aliasing constructor to create a shared pointer sharing lifetime
                  // with our shared ptr, but pointing to the true object
                  nr2shared_ptr.push_back(std::shared_ptr<void>(std::static_pointer_cast<void>(ptr),
                                                                info.downcaster(typeid(T),
                                                                                ptr.get())));
                }
              else
                nr2shared_ptr.push_back(ptr);
            }
          else
            {
              auto other = nr2shared_ptr[nr];
              bool neededDowncast;
              (*this) & neededDowncast;
              if(neededDowncast)
                {
                  // if there was a downcast we can expect the class to be registered (since archiving
                  // wouldn't have worked else)
                  std::string name;
                  (*this) & name;
                  auto info = GetArchiveRegister(name);
                  // same trick as above, create a shared ptr sharing lifetime with
                  // the shared_ptr<void> in the register, but pointing to our object
                  ptr = std::static_pointer_cast<T>(std::shared_ptr<void>(other,
                                                                          info.upcaster(typeid(T),
                                                                               other.get())));
                }
              else
                ptr = std::static_pointer_cast<T>(other);
            }
        }
      return *this;
    }

    // Archive pointers =======================================================
    template <typename T>
    Archive & operator& (T *& p)
    {
      if (Output())
        {
          // if the pointer is null store -2
          if (!p)
            return (*this) << -2;
          auto reg_ptr = static_cast<void*>(p);
          if(typeid(T) != typeid(*p))
            {
              if(!IsRegistered(Demangle(typeid(*p).name())))
                throw std::runtime_error(std::string("Archive error: Polymorphic type ")
                                         + Demangle(typeid(*p).name())
                                         + " not registered for archive");
              reg_ptr = GetArchiveRegister(Demangle(typeid(*p).name())).downcaster(typeid(T), static_cast<void*>(p));
            }
          auto pos = ptr2nr.find(reg_ptr);
          // if the pointer is not found in the map create a new entry
          if (pos == ptr2nr.end())
            {
              ptr2nr[reg_ptr] = ptr_count++;
              if(typeid(*p) == typeid(T))
                if (std::is_constructible<T>::value)
                               {
                                 return (*this) << -1 & (*p);
                               }
                else
                  throw std::runtime_error(std::string("Archive error: Class ") +
                                           Demangle(typeid(*p).name()) + " does not provide a default constructor!");
              else
                {
                  // if a pointer to a base class is archived, the class hierarchy must be registered
                  // to avoid compile time issues we allow this behaviour only for "our" classes that
                  // implement a void DoArchive(Archive&) member function
                  // To recreate the object we need to store the true type of it
                  if(!IsRegistered(Demangle(typeid(*p).name())))
                    throw std::runtime_error(std::string("Archive error: Polymorphic type ")
                                             + Demangle(typeid(*p).name())
                                             + " not registered for archive");
                  return (*this) << -3 << Demangle(typeid(*p).name()) & (*p);
                }
            }
          else
            {
              (*this) & pos->second;
              bool downcasted = !(reg_ptr == static_cast<void*>(p) );
              // store if the class has been downcasted and the name
              (*this) << downcasted << Demangle(typeid(*p).name());
            }
        }
      else
        {
          int nr;
          (*this) & nr;
          if (nr == -2) // restore a nullptr
              p = nullptr;
          else if (nr == -1) // create a new pointer of standard type (no virtual or multiple inheritance,...)
            {
              p = detail::constructIfPossible<T>();
              nr2ptr.push_back(p);
              (*this) & *p;
            }
          else if(nr == -3) // restore one of our registered classes that can have multiple inheritance,...
            {
              // As stated above, we want this special behaviour only for our classes that implement DoArchive
              std::string name;
              (*this) & name;
              auto info = GetArchiveRegister(name);
              // the creator creates a new object of type name, and returns a void* pointing
              // to T (which may have an offset)
              p = static_cast<T*>(info.creator(typeid(T)));
              // we store the downcasted pointer (to be able to find it again from
              // another class in a multiple inheritance tree)
              nr2ptr.push_back(info.downcaster(typeid(T),p));
              (*this) & *p;
            }
          else
            {
              bool downcasted;
              std::string name;
              (*this) & downcasted & name;
              if(downcasted)
                {
                  // if the class has been downcasted we can assume it is in the register
                  auto info = GetArchiveRegister(name);
                  p = static_cast<T*>(info.upcaster(typeid(T), nr2ptr[nr]));
                }
              else
                p = static_cast<T*>(nr2ptr[nr]);
            }
        }
      return *this;
    }

    // const ptr
    template<typename T>
    Archive& operator &(const T*& t)
    {
      return (*this) & const_cast<T*&>(t); // NOLINT
    }

    // Write a read only variable
    template <typename T>
    Archive & operator << (const T & t)
    {
      T ht(t);
      (*this) & ht;
      return *this;
    }

    virtual void FlushBuffer() {}

  protected:
    static std::map<std::string, VersionInfo>& GetLibraryVersions();

  private:
    template<typename T, typename ... Bases>
    friend class RegisterClassForArchive;

    // Returns ClassArchiveInfo of Demangled typeid
    static const detail::ClassArchiveInfo& GetArchiveRegister(const std::string& classname);
    // Set ClassArchiveInfo for Demangled typeid, this is done by creating an instance of
    // RegisterClassForArchive<type, bases...>
    static void SetArchiveRegister(const std::string& classname, const detail::ClassArchiveInfo& info);
    static bool IsRegistered(const std::string& classname);

    // Helper class for up-/downcasting
    template<typename T, typename ... Bases>
    struct Caster{};

    template<typename T>
    struct Caster<T>
    {
      static void* tryUpcast (const std::type_info& /*unused*/, T* /*unused*/)
      {
        throw std::runtime_error("Upcast not successful, some classes are not registered properly for archiving!");
      }
      static void* tryDowncast (const std::type_info& /*unused*/, void* /*unused*/)
      {
        throw std::runtime_error("Downcast not successful, some classes are not registered properly for archiving!");
      }
    };

    template<typename T, typename B1, typename ... Brest>
    struct Caster<T,B1,Brest...>
    {
      static void* tryUpcast(const std::type_info& ti, T* p)
      {
        try
          { return GetArchiveRegister(Demangle(typeid(B1).name())).
              upcaster(ti, static_cast<void*>(dynamic_cast<B1*>(p))); }
        catch(std::exception&)
          { return Caster<T, Brest...>::tryUpcast(ti, p); }
      }

      static void* tryDowncast(const std::type_info& ti, void* p)
      {
        if(typeid(B1) == ti)
          return dynamic_cast<T*>(static_cast<B1*>(p));
        try
          {
            return dynamic_cast<T*>(static_cast<B1*>(GetArchiveRegister(Demangle(typeid(B1).name())).
                                                     downcaster(ti, p)));
          }
        catch(std::exception&)
          {
            return Caster<T, Brest...>::tryDowncast(ti, p);
          }
      }
    };
  };

  template<typename T, typename ... Bases>
  class RegisterClassForArchive
  {
  public:
    RegisterClassForArchive()
    {
      static_assert(detail::all_of_tmpl<std::is_base_of<Bases,T>::value...>,
                    "Variadic template arguments must be base classes of T");
      detail::ClassArchiveInfo info;
      info.creator = [this,&info](const std::type_info& ti) -> void*
                     { return typeid(T) == ti ? detail::constructIfPossible<T>()
                         : Archive::Caster<T, Bases...>::tryUpcast(ti, detail::constructIfPossible<T>()); };
      info.upcaster = [this](const std::type_info& ti, void* p) -> void*
                      { return typeid(T) == ti ? p : Archive::Caster<T, Bases...>::tryUpcast(ti, static_cast<T*>(p)); };
      info.downcaster = [this](const std::type_info& ti, void* p) -> void*
                        { return typeid(T) == ti ? p : Archive::Caster<T, Bases...>::tryDowncast(ti, p); };
      Archive::SetArchiveRegister(std::string(Demangle(typeid(T).name())),info);
    }


  };

  // BinaryOutArchive ======================================================================
  class NGCORE_API BinaryOutArchive : public Archive
  {
    static constexpr size_t BUFFERSIZE = 1024;
    char buffer[BUFFERSIZE] = {};
    size_t ptr = 0;
    std::shared_ptr<std::ostream> fout;
  public:
    BinaryOutArchive() = delete;
    BinaryOutArchive(const BinaryOutArchive&) = delete;
    BinaryOutArchive(BinaryOutArchive&&) = delete;
    BinaryOutArchive(std::shared_ptr<std::ostream>&& afout)
      : Archive(true), fout(std::move(afout))
    {
      (*this) & GetLibraryVersions();
    }
    BinaryOutArchive(const std::string& filename)
      : BinaryOutArchive(std::make_shared<std::ofstream>(filename)) {}
    ~BinaryOutArchive () override { FlushBuffer(); }

    BinaryOutArchive& operator=(const BinaryOutArchive&) = delete;
    BinaryOutArchive& operator=(BinaryOutArchive&&) = delete;

    const VersionInfo& GetVersion(const std::string& library) override
    { return GetLibraryVersions()[library]; }

    using Archive::operator&;
    Archive & operator & (double & d) override
    { return Write(d); }
    Archive & operator & (int & i) override
    { return Write(i); }
    Archive & operator & (short & i) override
    { return Write(i); }
    Archive & operator & (long & i) override
    { return Write(i); }
    Archive & operator & (size_t & i) override
    { return Write(i); }
    Archive & operator & (unsigned char & i) override
    { return Write(i); }
    Archive & operator & (bool & b) override
    { return Write(b); }
    Archive & operator & (std::string & str) override
    {
      int len = str.length();
      (*this) & len;
      FlushBuffer();
      if(len)
        fout->write (&str[0], len);
      return *this;
    }
    Archive & operator & (char *& str) override
    {
      long len = str ? strlen (str) : -1;
      (*this) & len;
      FlushBuffer();
      if(len > 0)
        fout->write (&str[0], len); // NOLINT
      return *this;
    }
    void FlushBuffer() override
    {
      if (ptr > 0)
        {
          fout->write(&buffer[0], ptr);
          ptr = 0;
        }
    }

  private:
    template <typename T>
    Archive & Write (T x)
    {
      if (unlikely(ptr > BUFFERSIZE-sizeof(T)))
        {
          fout->write(&buffer[0], ptr);
          *reinterpret_cast<T*>(&buffer[0]) = x; // NOLINT
          ptr = sizeof(T);
          return *this;
        }
      *reinterpret_cast<T*>(&buffer[ptr]) = x; // NOLINT
      ptr += sizeof(T);
      return *this;
    }
  };

  // BinaryInArchive ======================================================================
  class NGCORE_API BinaryInArchive : public Archive
  {
    std::map<std::string, VersionInfo> vinfo{};
    std::shared_ptr<std::istream> fin;
  public:
    BinaryInArchive (std::shared_ptr<std::istream>&& afin)
      : Archive(false), fin(std::move(afin))
    {
      (*this) & vinfo;
    }
    BinaryInArchive (const std::string& filename)
      : BinaryInArchive(std::make_shared<std::ifstream>(filename)) { ; }

    const VersionInfo& GetVersion(const std::string& library) override
    { return vinfo[library]; }

    using Archive::operator&;
    Archive & operator & (double & d) override
    { Read(d); return *this; }
    Archive & operator & (int & i) override
    { Read(i); return *this; }
    Archive & operator & (short & i) override
    { Read(i); return *this; }
    Archive & operator & (long & i) override
    { Read(i); return *this; }
    Archive & operator & (size_t & i) override
    { Read(i); return *this; }
    Archive & operator & (unsigned char & i) override
    { Read(i); return *this; }
    Archive & operator & (bool & b) override
    { Read(b); return *this; }
    Archive & operator & (std::string & str) override
    {
      int len;
      (*this) & len;
      str.resize(len);
      if(len)
        fin->read(&str[0], len); // NOLINT
      return *this;
    }
    Archive & operator & (char *& str) override
    {
      long len;
      (*this) & len;
      if(len == -1)
        str = nullptr;
      else
        {
          str = new char[len+1]; // NOLINT
          fin->read(&str[0], len); // NOLINT
          str[len] = '\0'; // NOLINT
        }
      return *this;
    }

    Archive & Do (double * d, size_t n) override
    { fin->read(reinterpret_cast<char*>(d), n*sizeof(double)); return *this; } // NOLINT
    Archive & Do (int * i, size_t n) override
    { fin->read(reinterpret_cast<char*>(i), n*sizeof(int)); return *this; } // NOLINT
    Archive & Do (size_t * i, size_t n) override
    { fin->read(reinterpret_cast<char*>(i), n*sizeof(size_t)); return *this; } // NOLINT

  private:
    template<typename T>
    inline void Read(T& val)
    { fin->read(reinterpret_cast<char*>(&val), sizeof(T)); } // NOLINT
  };

  // TextOutArchive ======================================================================
  class NGCORE_API TextOutArchive : public Archive
  {
    std::shared_ptr<std::ostream> fout;
  public:
    TextOutArchive (std::shared_ptr<std::ostream>&& afout)
      : Archive(true), fout(std::move(afout))
    {
      (*this) & GetLibraryVersions();
    }
    TextOutArchive (const std::string& filename) :
      TextOutArchive(std::make_shared<std::ofstream>(filename)) { }

    const VersionInfo& GetVersion(const std::string& library) override
    { return GetLibraryVersions()[library]; }

    using Archive::operator&;
    Archive & operator & (double & d) override
    { *fout << d << '\n'; return *this; }
    Archive & operator & (int & i) override
    { *fout << i << '\n'; return *this; }
    Archive & operator & (short & i) override
    { *fout << i << '\n'; return *this; }
    Archive & operator & (long & i) override
    { *fout << i << '\n'; return *this; }
    Archive & operator & (size_t & i) override
    { *fout << i << '\n'; return *this; }
    Archive & operator & (unsigned char & i) override
    { *fout << int(i) << '\n'; return *this; }
    Archive & operator & (bool & b) override
    { *fout << (b ? 't' : 'f') << '\n'; return *this; }
    Archive & operator & (std::string & str) override
    {
      int len = str.length();
      *fout << len << '\n';
      if(len)
        {
          fout->write(&str[0], len); // NOLINT
          *fout << '\n';
        }
      return *this;
    }
    Archive & operator & (char *& str) override
    {
      long len = str ? strlen (str) : -1;
      *this & len;
      if(len > 0)
        {
          fout->write (&str[0], len); // NOLINT
          *fout << '\n';
        }
      return *this;
    }
  };

  // TextInArchive ======================================================================
  class NGCORE_API TextInArchive : public Archive
  {
    std::map<std::string, VersionInfo> vinfo{};
    std::shared_ptr<std::istream> fin;
  public:
    TextInArchive (std::shared_ptr<std::istream>&& afin) :
      Archive(false), fin(std::move(afin))
    {
      (*this) & vinfo;
    }
    TextInArchive (const std::string& filename)
      : TextInArchive(std::make_shared<std::ifstream>(filename)) {}

    const VersionInfo& GetVersion(const std::string& library) override
    { return vinfo[library]; }

    using Archive::operator&;
    Archive & operator & (double & d) override
    { *fin >> d; return *this; }
    Archive & operator & (int & i) override
    { *fin >> i; return *this; }
    Archive & operator & (short & i) override
    { *fin >> i; return *this; }
    Archive & operator & (long & i) override
    { *fin >> i; return *this; }
    Archive & operator & (size_t & i) override
    { *fin >> i; return *this; }
    Archive & operator & (unsigned char & i) override
    { int _i; *fin >> _i; i = _i; return *this; }
    Archive & operator & (bool & b) override
    { char c; *fin >> c; b = (c=='t'); return *this; }
    Archive & operator & (std::string & str) override
    {
      int len;
      *fin >> len;
      char ch;
      fin->get(ch); // '\n'
      str.resize(len);
      if(len)
        fin->get(&str[0], len+1, '\0');
      return *this;
    }
    Archive & operator & (char *& str) override
    {
      long len;
      (*this) & len;
      char ch;
      if(len == -1)
        {
          str = nullptr;
          return (*this);
        }
      str = new char[len+1]; // NOLINT
      if(len)
        {
          fin->get(ch); // \n
          fin->get(&str[0], len+1, '\0'); // NOLINT
        }
      str[len] = '\0'; // NOLINT
      return *this;
    }
  };
} // namespace ngcore

#endif // NETGEN_CORE_ARCHIVE_HPP
