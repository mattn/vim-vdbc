// The MIT License (MIT)
//
// Copyright (c) 2014 kamichidu
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
#include "pg_libpq.h"

#include "picojson.h"
#include <libpq-fe.h>
#include <map>
#include <memory>
#include <utility>
#include <sstream>
#include <stdint.h>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace
{
    namespace json= picojson;

    std::pair<json::object, std::string> const parse_json(std::string const& json)
    {
        typedef std::pair<json::object, std::string> return_type;

        json::value root;
        std::string error;

        json::parse(root, json.begin(), json.end(), &error);

        if(root.is<json::object>())
        {
            return return_type(root.get<json::object>(), error);
        }
        else
        {
            return return_type(json::object(), "it's not a object type");
        }
    }

    int const generate_id()
    {
        static int current_id= 0;

        return ++current_id;
    }

    std::string const for_error(std::string const& message)
    {
        json::object obj;

        obj["success"]= json::value(0.);
        obj["message"]= json::value(message);

        return json::value(obj).serialize();
    }

    std::string const ptr2str(void const* const p)
    {
        std::stringstream ss;

        ss << p;

        return ss.str();
    }

    void* const str2ptr(std::string const& s)
    {
        std::stringstream ss(s);
        void* p;

        ss >> p;

        return p;
    }

    static std::map<int, std::shared_ptr<PGconn>> connections;
}

char const* const vdbc_pg_libpq_initialize(char const* const libname)
{
    static std::string r;

#ifdef _WIN32
    void* handle= LoadLibrary(libname);
#else
    void* handle= dlopen(libname, RTLD_NOW);

    if(!handle)
    {
        return (r= for_error(dlerror())).c_str();
    }
#endif

    json::object retobj;

    retobj["success"]= json::value(1.);
    retobj["handle"]=  json::value(ptr2str(handle));

    return (r= json::value(retobj).serialize()).c_str();
}

char const* const vdbc_pg_libpq_terminate(char const* const handle)
{
    static std::string r;

#ifdef _WIN32
    void* p= str2ptr(handle);

    FreeLibrary(static_cast<HMODULE>(p));
#else
    void* p= str2ptr(handle);

    if(!p)
    {
        return (r= for_error("null handle")).c_str();
    }

    if(dlclose(p) != 0)
    {
        return (r= for_error(dlerror())).c_str();
    }
#endif

    json::object retobj;

    retobj["success"]= json::value(1.);

    return (r= json::value(retobj).serialize()).c_str();
}

char const* const vdbc_pg_libpq_connect(char const* const a)
{
    static std::string r;

    json::object args;
    {
        std::pair<json::object, std::string> const parsed= parse_json(a);

        if(!parsed.second.empty())
        {
            return (r= for_error(parsed.second)).c_str();
        }

        args= parsed.first;
    }

    std::stringstream conninfo;

    if(args.find("host") != args.end() && args["host"].is<std::string>())
    {
        conninfo << " host=" << args["host"].get<std::string>();
    }
    if(args.find("port") != args.end() && args["port"].is<double>())
    {
        conninfo << " port=" << static_cast<int>(args["port"].get<double>());
    }
    if(args.find("user") != args.end() && args["user"].is<std::string>())
    {
        conninfo << " user=" << args["user"].get<std::string>();
    }
    if(args.find("password") != args.end() && args["password"].is<std::string>())
    {
        conninfo << " password=" << args["password"].get<std::string>();
    }
    if(args.find("dbname") != args.end() && args["dbname"].is<std::string>())
    {
        conninfo << " dbname=" << args["dbname"].get<std::string>();
    }

    std::shared_ptr<PGconn> conn(PQconnectdb(conninfo.str().c_str()), [](PGconn* p){
        if(p)
        {
            PQfinish(p);
        }
    });

    if(PQstatus(conn.get()) != CONNECTION_OK)
    {
        return (r= for_error(PQerrorMessage(conn.get()))).c_str();
    }

    int const id= generate_id();
    connections[id]= conn;

    json::object retobj;

    retobj["success"]= json::value(1.);
    retobj["id"]=      json::value(static_cast<double>(id));

    return (r= json::value(retobj).serialize()).c_str();
}

char const* const vdbc_pg_libpq_disconnect(char const* const a)
{
    static std::string r;

    json::object args;
    {
        std::pair<json::object, std::string> const parsed= parse_json(a);

        if(!parsed.second.empty())
        {
            return (r= for_error(parsed.second)).c_str();
        }

        args= parsed.first;
    }

    int const id= static_cast<int>(args["id"].get<double>());
    if(connections.find(id) != connections.end())
    {
        connections.erase(id);

        json::object retobj;

        retobj["success"]= json::value(1.);

        return (r= json::value(retobj).serialize()).c_str();
    }
    else
    {
        return (r= for_error("unknown id")).c_str();
    }
}

char const* const vdbc_pg_libpq_select_as_list(char const* const a)
{
    static std::string r;

    json::object args;
    {
        std::pair<json::object, std::string> const parsed= parse_json(a);

        if(!parsed.second.empty())
        {
            return (r= for_error(parsed.second)).c_str();
        }

        args= parsed.first;
    }

    int const id= static_cast<int>(args["id"].get<double>());
    if(connections.find(id) == connections.end())
    {
        return (r= for_error("unknown id")).c_str();
    }
    std::string const query= args["query"].get<std::string>();

    std::shared_ptr<PGconn> const conn= connections.at(id);
    std::shared_ptr<PGresult> const query_result(PQexec(conn.get(), query.c_str()), [](PGresult* p){
        if(p)
        {
            PQclear(p);
        }
    });

    if(!query_result)
    {
        return (r= for_error(PQerrorMessage(conn.get()))).c_str();
    }

    switch(PQresultStatus(query_result.get()))
    {
        case PGRES_TUPLES_OK:
            break;
        default:
            return (r= for_error(PQresultErrorMessage(query_result.get()))).c_str();
    }

    int const nfields= PQnfields(query_result.get());
    int const ntuples= PQntuples(query_result.get());
    json::array tuples;
    for(int row= 0; row < ntuples; ++row)
    {
        json::array fields;
        for(int col= 0; col < nfields; ++col)
        {
            if(PQbinaryTuples(query_result.get()))
            {
                int const byte_length= PQgetlength(query_result.get(), row, col);
                char const* const binary= PQgetvalue(query_result.get(), row, col);
                std::stringstream ss;

                for(int offset= 0; offset < byte_length; ++offset)
                {
                    int8_t const byte= static_cast<int8_t>(*(binary + offset));

                    ss << byte;
                }

                fields.push_back(json::value(ss.str()));
            }
            else
            {
                fields.push_back(json::value(std::string(PQgetvalue(query_result.get(), row, col))));
            }
        }

        tuples.push_back(json::value(fields));
    }

    json::object retobj;

    retobj["success"]= json::value(1.);
    retobj["result"]=  json::value(tuples);

    return (r= json::value(retobj).serialize()).c_str();
}

char const* const vdbc_pg_libpq_select_as_dict(char const* const a)
{
    static std::string r;

    json::object args;
    {
        std::pair<json::object, std::string> const parsed= parse_json(a);

        if(!parsed.second.empty())
        {
            return (r= for_error(parsed.second)).c_str();
        }

        args= parsed.first;
    }

    int const id= static_cast<int>(args["id"].get<double>());
    if(connections.find(id) == connections.end())
    {
        return (r= for_error("unknown id")).c_str();
    }
    std::string const query= args["query"].get<std::string>();

    std::shared_ptr<PGconn> const conn= connections.at(id);
    std::shared_ptr<PGresult> const query_result(PQexec(conn.get(), query.c_str()), [](PGresult* p){
        if(p)
        {
            PQclear(p);
        }
    });

    if(!query_result)
    {
        return (r= for_error(PQerrorMessage(conn.get()))).c_str();
    }

    switch(PQresultStatus(query_result.get()))
    {
        case PGRES_TUPLES_OK:
            break;
        default:
            return (r= for_error(PQresultErrorMessage(query_result.get()))).c_str();
    }

    int const nfields= PQnfields(query_result.get());
    int const ntuples= PQntuples(query_result.get());
    json::array tuples;
    for(int row= 0; row < ntuples; ++row)
    {
        json::object fields;
        for(int col= 0; col < nfields; ++col)
        {
            std::string const label(PQfname(query_result.get(), col));

            if(PQbinaryTuples(query_result.get()))
            {
                int const byte_length= PQgetlength(query_result.get(), row, col);
                char const* const binary= PQgetvalue(query_result.get(), row, col);
                std::stringstream ss;

                for(int offset= 0; offset < byte_length; ++offset)
                {
                    int8_t const byte= static_cast<int8_t>(*(binary + offset));

                    ss << byte;
                }

                fields[label]= json::value(ss.str());
            }
            else
            {
                fields[label]= json::value(std::string(PQgetvalue(query_result.get(), row, col)));
            }
        }

        tuples.push_back(json::value(fields));
    }

    json::object retobj;

    retobj["success"]= json::value(1.);
    retobj["result"]=  json::value(tuples);

    return (r= json::value(retobj).serialize()).c_str();
}
