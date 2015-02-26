/*
Copyright (c) 2013, David C Horton

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <errno.h>
#include <stdio.h>

#include <boost/tokenizer.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>
#include <boost/thread.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/foreach.hpp>
#include <boost/algorithm/string/find.hpp>
#include <boost/regex.hpp>

#include <sofia-sip/url.h>
#include <sofia-sip/nta_tport.h>
#include <sofia-sip/tport.h>
#include <sofia-sip/msg.h>
#include <sofia-sip/msg_addr.h>
#include <sofia-sip/su_string.h>
#include <sofia-sip/su_addrinfo.h>


#include "drachtio.h"
#include "controller.hpp"

#define MAX_LINELEN 2047

using namespace std ;
 
namespace {
    unsigned int json_allocs = 0 ;
    unsigned int json_bytes = 0 ;
    boost::mutex  json_lock ;
} ;

namespace drachtio {

    typedef boost::unordered_map<string,tag_type_t> mapHdr2Tag ;

    typedef boost::unordered_set<string> setHdr ;

    typedef boost::unordered_map<string,sip_method_t> mapMethod2Type ;

	/* headers that are allowed to be set by the client in responses to sip requests */
	mapHdr2Tag m_mapHdr2Tag = boost::assign::map_list_of
		( string("user_agent"), siptag_user_agent_str ) 
        ( string("subject"), siptag_subject_str ) 
        ( string("max_forwards"), siptag_max_forwards_str ) 
        ( string("proxy_require"), siptag_proxy_require_str ) 
        ( string("accept_contact"), siptag_accept_contact_str ) 
        ( string("reject_contact"), siptag_reject_contact_str ) 
        ( string("expires"), siptag_expires_str ) 
        ( string("date"), siptag_date_str ) 
        ( string("retry_after"), siptag_retry_after_str ) 
        ( string("timestamp"), siptag_timestamp_str ) 
        ( string("min_expires"), siptag_min_expires_str ) 
        ( string("priority"), siptag_priority_str ) 
        ( string("call_info"), siptag_call_info_str ) 
        ( string("organization"), siptag_organization_str ) 
        ( string("server"), siptag_server_str ) 
        ( string("in_reply_to"), siptag_in_reply_to_str ) 
        ( string("accept"), siptag_accept_str ) 
        ( string("accept_encoding"), siptag_accept_encoding_str ) 
        ( string("accept_language"), siptag_accept_language_str ) 
        ( string("allow"), siptag_allow_str ) 
        ( string("require"), siptag_require_str ) 
        ( string("supported"), siptag_supported_str ) 
        ( string("unsupported"), siptag_unsupported_str ) 
        ( string("event"), siptag_event_str ) 
        ( string("allow_events"), siptag_allow_events_str ) 
        ( string("subscription_state"), siptag_subscription_state_str ) 
        ( string("proxy_authenticate"), siptag_proxy_authenticate_str ) 
        ( string("proxy_authentication_info"), siptag_proxy_authentication_info_str ) 
        ( string("proxy_authorization"), siptag_proxy_authorization_str ) 
        ( string("authorization"), siptag_authorization_str ) 
        ( string("www_authenticate"), siptag_www_authenticate_str ) 
        ( string("authentication_info"), siptag_authentication_info_str ) 
        ( string("error_info"), siptag_error_info_str ) 
        ( string("warning"), siptag_warning_str ) 
        ( string("refer_to"), siptag_refer_to_str ) 
        ( string("referred_by"), siptag_referred_by_str ) 
        ( string("replaces"), siptag_replaces_str ) 
        ( string("session_expires"), siptag_session_expires_str ) 
        ( string("min_se"), siptag_min_se_str ) 
        ( string("path"), siptag_path_str ) 
        ( string("service_route"), siptag_service_route_str ) 
        ( string("reason"), siptag_reason_str ) 
        ( string("security_client"), siptag_security_client_str ) 
        ( string("security_server"), siptag_security_server_str ) 
        ( string("security_verify"), siptag_security_verify_str ) 
        ( string("privacy"), siptag_privacy_str ) 
        ( string("etag"), siptag_etag_str ) 
        ( string("if_match"), siptag_if_match_str ) 
        ( string("mime_version"), siptag_mime_version_str ) 
        ( string("content_type"), siptag_content_type_str ) 
        ( string("content_encoding"), siptag_content_encoding_str ) 
        ( string("content_language"), siptag_content_language_str ) 
        ( string("content_disposition"), siptag_content_disposition_str ) 
        ( string("request_disposition"), siptag_request_disposition_str ) 
        ( string("error"), siptag_error_str ) 
        ( string("refer_sub"), siptag_refer_sub_str ) 
        ( string("alert_info"), siptag_alert_info_str ) 
        ( string("reply_to"), siptag_reply_to_str ) 
        ( string("p_asserted_identity"), siptag_p_asserted_identity_str ) 
        ( string("p_preferred_identity"), siptag_p_preferred_identity_str ) 
        ( string("remote_party_id"), siptag_remote_party_id_str ) 
        ( string("payload"), siptag_payload_str ) 
        ( string("from"), siptag_from_str ) 
        ( string("to"), siptag_from_str ) 
        ( string("call_id"), siptag_call_id_str ) 
        ( string("cseq"), siptag_cseq_str ) 
        ( string("via"), siptag_via_str ) 
        ( string("route"), siptag_route_str ) 
        ( string("contact"), siptag_contact_str ) 
        ( string("rseq"), siptag_rseq_str ) 
        ( string("rack"), siptag_rack_str ) 
        ( string("record_route"), siptag_record_route_str ) 
        ( string("content_length"), siptag_content_length_str ) 
		;

	/* headers that are not allowed to be set by the client in responses to sip requests */
	setHdr m_setImmutableHdrs = boost::assign::list_of
		( string("from") ) 
		( string("to") ) 
		( string("call_id") ) 
		( string("cseq") ) 
        ( string("via") ) 
        ( string("route") ) 
//        ( string("contact") ) 
        ( string("rseq") ) 
//        ( string("rack") ) 
        ( string("record_route") ) 
        ( string("content_length") ) 
		;

   mapMethod2Type m_mapMethod2Type = boost::assign::map_list_of
        ( string("INVITE"), sip_method_invite ) 
        ( string("ACK"), sip_method_ack ) 
        ( string("CANCEL"), sip_method_cancel ) 
        ( string("BYE"), sip_method_bye ) 
        ( string("OPTIONS"), sip_method_options ) 
        ( string("REGISTER"), sip_method_register ) 
        ( string("INFO"), sip_method_info ) 
        ( string("PRACK"), sip_method_prack ) 
        ( string("UPDATE"), sip_method_update ) 
        ( string("MESSAGE"), sip_method_message ) 
        ( string("SUBSCRIBE"), sip_method_subscribe ) 
        ( string("NOTIFY"), sip_method_notify ) 
        ( string("REFER"), sip_method_refer ) 
        ( string("PUBLISH"), sip_method_publish ) 
        ;


	bool isImmutableHdr( const string& hdr ) {
		return m_setImmutableHdrs.end() != m_setImmutableHdrs.find( hdr ) ;
	}

	bool getTagTypeForHdr( const string& hdr, tag_type_t& tag ) {
		mapHdr2Tag::const_iterator it = m_mapHdr2Tag.find( hdr ) ;
		if( it != m_mapHdr2Tag.end() ) {
		    tag = it->second ;
		    return true ;
		}		
		return false ;
	}

	void generateUuid(string& uuid) {
	    boost::uuids::uuid id = boost::uuids::random_generator()();
        uuid = boost::lexical_cast<string>(id) ;
    }	

	void parseGenericHeader( msg_common_t* p, string& hvalue) {
		string str((const char*) p->h_data, p->h_len) ;
		boost::char_separator<char> sep(": \r\n") ;
        tokenizer tok( str, sep) ;
        if( distance( tok.begin(), tok.end() ) > 1 ) hvalue = *(++tok.begin() ) ;
 	}

    bool FindCSeqMethod( const string& headers, string& method ) {
        boost::regex e("^CSeq:\\s+\\d+\\s+(\\w+)$", boost::regex::extended);
        boost::smatch mr;
        if( boost::regex_search( headers, mr, e ) ) {
            method = mr[1] ;
            return true ;
        }
        return false ;
    }

    void EncodeStackMessage( const sip_t* sip, string& encodedMessage ) {
        encodedMessage.clear() ;
        const sip_common_t* p = NULL ;
        if( sip->sip_request ) {
            sip_header_t* hdr = (sip_header_t *) sip->sip_request ;
            p = hdr->sh_common ;
        }
        else if( sip->sip_status ) {
            sip_header_t* hdr = (sip_header_t *) sip->sip_status ;
            p = hdr->sh_common ;
        }

        while( NULL != p) {
            if( NULL != p->h_data ) {
                //take the original fragment if it exists since this will be more efficient
               encodedMessage.append( (char *)p->h_data, p->h_len ) ;            
            }
            else {
                //otherwise, encode the sip header 
                char buf[1024] ;
                issize_t n = msg_header_e(buf, 1024, reinterpret_cast<const msg_header_t *>(p), 0) ;
                encodedMessage.append( buf, n ) ;
            }
            p = p->h_succ->sh_common ;
        }
    }

    bool normalizeSipUri( std::string& uri ) {
        url_t url ;
        char s[255] ;
        char hp[64] ;

        /* decode the url */
        strncpy( s, uri.c_str(), 255 ) ;
        if( url_d(&url, s ) < 0 ) {
            DR_LOG(log_error) << "normalizeSipUri: invalid url " << uri << endl ;
            return false ;       
          }

        /* we have may just been given a user part */
        if( NULL == url.url_scheme ) {
            uri = "sip:" + uri ;
            if( NULL == url.url_user ) uri = uri + "@localhost" ;
            strncpy( s, uri.c_str(), 255 ) ;
            if( url_d(&url, s ) < 0 ) {
                DR_LOG(log_error) << "normalizeSipUri: invalid url " << uri << endl ;
                return false ;       
            }   
         }

        /* encode the url */
        if( url_e(s, 255, &url) < 0 ) {
           DR_LOG(log_error) << "normalizeSipUri: error encoding url  " << s << endl ;
            return false ;
        }

        uri.assign( s ) ;
        return true ;
    }

    bool replaceHostInUri( std::string& uri, const std::string& hostport ) {
        url_t url ;
        char s[255] ;
        char hp[64] ;

        /* decode the url */
        strncpy( s, uri.c_str(), 255 ) ;
        if( url_d(&url, s ) < 0 ) {
            DR_LOG(log_error) << "replaceHostInUri: invalid url " << uri << endl ;
            return false ;       
          }

        /* we have may just been given a user part */
        if( NULL == url.url_scheme ) {
            uri = "sip:" + uri ;
            if( NULL == url.url_user ) uri = uri + "@localhost" ;
            strncpy( s, uri.c_str(), 255 ) ;
            if( url_d(&url, s ) < 0 ) {
                DR_LOG(log_error) << "replaceHostInUri: invalid url " << uri << endl ;
                return false ;       
            }   
         }

        /* insert the provided host and port */
        url.url_host = NULL ;
        url.url_port = NULL ;

        strcpy( hp, hostport.c_str() ) ;
        int n = strcspn(hp, ":");
        if( hp[n] == ':' ) {
            hp[n] = '\0' ;
            url.url_port = hp + n + 1 ;
        }
        url.url_host = hp ;

        /* encode the url */
        if( url_e(s, 255, &url) < 0 ) {
           DR_LOG(log_error) << "replaceHostInUri: error encoding url  " << s << endl ;
            return false ;
        }

        uri.assign( s ) ;
        return true ;
    }

    sip_method_t methodType( const string& method ) {
        mapMethod2Type::const_iterator it = m_mapMethod2Type.find( method ) ;
        if( m_mapMethod2Type.end() == it ) return sip_method_unknown ;
        return it->second ;
    }
 
    bool isLocalSipUri( const string& requestUri ) {

        static bool initialized = false ;
        static boost::unordered_set<string> setLocalUris ;

        if( !initialized ) {
            initialized = true ;

            nta_agent_t* agent = theOneAndOnlyController->getAgent() ;
            tport_t *t = nta_agent_tports( agent ) ;
            for (tport_t* tport = t; tport; tport = tport_next(tport) ) {
                const tp_name_t* tpn = tport_name( tport );
                if( 0 == strcmp( tpn->tpn_host, "*") ) 
                    continue ;

                string localUri = tpn->tpn_host ;
                localUri += ":" ;
                localUri += (tpn->tpn_port ? tpn->tpn_port : "5060");

                setLocalUris.insert( localUri ) ;

                if( 0 == strcmp(tpn->tpn_host,"127.0.0.1") ) {
                    localUri = "localhost:" ;
                    localUri += (tpn->tpn_port ? tpn->tpn_port : "5060");
                    setLocalUris.insert( localUri ) ;
                }
            }
       }

        url_t *url = url_make(theOneAndOnlyController->getHome(), requestUri.c_str() ) ;
        string uri = url->url_host ;
        uri += ":" ;
        uri += ( url->url_port ? url->url_port : "5060") ;

        return setLocalUris.end() != setLocalUris.find( uri ) ;
    }

    void* my_json_malloc( size_t bytes ) {
        boost::lock_guard<boost::mutex> l( json_lock ) ;

        json_allocs++ ;
        json_bytes += bytes ;
        //DR_LOG(log_debug) << "my_json_malloc: alloc'ing " << bytes << " bytes; outstanding allocations: " << json_allocs << ", outstanding memory size: " << json_bytes << endl ;

        /* store size at the beginnng of the block */
        void *ptr = malloc( bytes + 8 ) ;
        *((size_t *)ptr) = bytes ;
 
        return (void*) ((char*) ptr + 8);
    }

    void my_json_free( void* ptr ) {
       boost::lock_guard<boost::mutex> l( json_lock ) ;

        size_t size;
        ptr = (void *) ((char *) ptr - 8) ;
        size = *((size_t *)ptr);

        json_allocs-- ;
        json_bytes -= size ;
        //DR_LOG(log_debug) << "my_json_free: freeing " << size << " bytes; outstanding allocations: " << json_allocs << ", outstanding memory size: " << json_bytes << endl ;

        /* zero memory in debug mode */
        memset( ptr, 0, size + 8 ) ;

    }

    void splitLines( const string& s, vector<string>& vec ) {
        split( vec, s, boost::is_any_of("\r\n"), boost::token_compress_on ); 
    }

    void splitTokens( const string& s, vector<string>& vec ) {
        split( vec, s, boost::is_any_of("|") /*, boost::token_compress_on */); 
    }

    void splitMsg( const string& msg, string& meta, string& startLine, string& headers, string& body ) {
        size_t pos = msg.find( CRLF ) ;
        if( string::npos == pos ) {
            meta = msg ;
            return ;
        }
        meta = msg.substr(0, pos) ;
        string chunk = msg.substr(pos+CRLF.length()) ;

        pos = chunk.find( CRLF2 ) ;
        if( string::npos != pos  ) {
            body = chunk.substr( pos + CRLF2.length() ) ;
            chunk = chunk.substr( 0, pos ) ;
        }

        pos = chunk.find( CRLF ) ;
        if( string::npos == pos ) {
            startLine = chunk ;
        }
        else {
            startLine = chunk.substr(0, pos) ;
            headers = chunk.substr(pos + CRLF.length()) ;
        }
    }

    sip_method_t parseStartLine( const string& startLine, string& methodName, string& requestUri ) {
        boost::char_separator<char> sep(" ");
        boost::tokenizer< boost::char_separator<char> > tokens(startLine, sep);
        int i = 0 ;
        sip_method_t method = sip_method_invalid ;
        BOOST_FOREACH (const string& t, tokens) {
            switch( i++ ) {
                case 0:
                    methodName = t ;
                    if( 0 == t.compare("INVITE") ) method = sip_method_invite ;
                    else if( 0 == t.compare("ACK") ) method = sip_method_ack ;
                    else if( 0 == t.compare("PRACK") ) method = sip_method_prack ;
                    else if( 0 == t.compare("CANCEL") ) method = sip_method_cancel ;
                    else if( 0 == t.compare("BYE") ) method = sip_method_bye ;
                    else if( 0 == t.compare("OPTIONS") ) method = sip_method_options ;
                    else if( 0 == t.compare("REGISTER") ) method = sip_method_register ;
                    else if( 0 == t.compare("INFO") ) method = sip_method_info ;
                    else if( 0 == t.compare("UPDATE") ) method = sip_method_update ;
                    else if( 0 == t.compare("MESSAGE") ) method = sip_method_message ;
                    else if( 0 == t.compare("SUBSCRIBE") ) method = sip_method_subscribe ;
                    else if( 0 == t.compare("NOTIFY") ) method = sip_method_notify ;
                    else if( 0 == t.compare("REFER") ) method = sip_method_refer ;
                    else if( 0 == t.compare("PUBLISH") ) method = sip_method_publish ;
                    else method = sip_method_unknown ;
                    break ;

                case 1:
                    requestUri = t ;
                    break ;

                default:
                break ;
            }
        }
        return method ;
    }

    bool GetValueForHeader( const string& headers, const char *szHeaderName, string& headerValue ) {
        vector<string> vec ;
        splitLines( headers, vec ) ;
        for( std::vector<string>::const_iterator it = vec.begin(); it != vec.end(); ++it )  {
            string hdrName ;
            size_t pos = (*it).find_first_of(":") ;
            if( string::npos != pos ) {
                hdrName = (*it).substr(0,pos) ;
                boost::trim( hdrName );

                if( boost::iequals( hdrName, szHeaderName ) ) {
                    headerValue = (*it).substr(pos+1) ;
                    boost::trim( headerValue ) ;    
                    return true ;                
                }
            }
        }
        return false ;
    }
    void deleteTags( tagi_t* tags ) {
        int i = 0 ;
        while( tags[i].t_tag != tag_null ) {
            if( tags[i].t_value ) {
                char *p = (char *) tags[i].t_value ;
                delete [] p ;
            }
             i++ ;
        }       
        delete [] tags ; 
    }

    tagi_t* makeTags( const string&  hdrs ) {
        vector<string> vec ;

        splitLines( hdrs, vec ) ;
        int nHdrs = vec.size() ;
        tagi_t *tags = new tagi_t[nHdrs+1] ;
        int i = 0; 
        for( std::vector<string>::const_iterator it = vec.begin(); it != vec.end(); ++it )  {
            tags[i].t_tag = tag_skip ;
            tags[i].t_value = (tag_value_t) 0 ;                     
            bool bValid = true ;
            string hdrName, hdrValue ;

            //parse header name and value
            size_t pos = (*it).find_first_of(":") ;
            if( string::npos == pos ) {
                bValid = false ;
            }
            else {
                hdrName = (*it).substr(0,pos) ;
                boost::trim( hdrName );
                if( string::npos != hdrName.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890-_") ) {
                    bValid = false ;
                }
                else {
                    hdrValue = (*it).substr(pos+1) ;
                    boost::trim( hdrValue ) ;
                }
            }
            if( !bValid ) {
                DR_LOG(log_error) << "SipDialogController::makeTags - invalid header: '" << *it << "'"  ;
                i++ ;
                continue ;
            }
            else if( string::npos != hdrValue.find(CRLF) ) {
                DR_LOG(log_error) << "SipDialogController::makeTags - client supplied invalid custom header value (contains CR or LF) for header '" << hdrName << "'" ;
                i++ ;
                continue ;
            }

            //treat well-known headers differently than custom headers 
            tag_type_t tt ;
            string hdr = boost::to_lower_copy( boost::replace_all_copy( hdrName, "-", "_" ) );
            if( isImmutableHdr( hdr ) ) {
                //DR_LOG(log_debug) << "SipDialogController::makeTags - discarding header because client is not allowed to set dialog-level headers: '" << hdrName  ;
            }
            else if( getTagTypeForHdr( hdr, tt ) ) {
                //well-known header
                int len = hdrValue.length() ;
                char *p = new char[len+1] ;
                memset(p, '\0', len+1) ;
                strncpy( p, hdrValue.c_str(), len ) ;
                tags[i].t_tag = tt;
                tags[i].t_value = (tag_value_t) p ;
                //DR_LOG(log_debug) << "SipDialogController::makeTags - Adding well-known header '" << hdrName << "' with value '" << p << "'"  ;
            }
            else {
                //custom header
                int len = (*it).length() ;                  
                char *p = new char[len+1] ;
                memset(p, '\0', len+1) ;
                strncpy( p, (*it).c_str(), len) ;

                tags[i].t_tag = siptag_unknown_str ;
                tags[i].t_value = (tag_value_t) p ;
                //DR_LOG(log_debug) << "SipDialogController::makeTags - custom header: '" << hdrName << "', value: " << hdrValue  ;  
            }

            i++ ;
        }
        tags[nHdrs].t_tag = tag_null ;
        tags[nHdrs].t_value = (tag_value_t) 0 ;       

        return tags ;   //NB: caller responsible to delete after use to free memory      
    }
 
    SipMsgData_t::SipMsgData_t(const string& str ) {
        boost::char_separator<char> sep(" []//:") ;
        tokenizer tok( str, sep) ;
        tokenizer::iterator it = tok.begin() ;

        m_source = 0 == (*it).compare("recv") ? "network" : "application" ;
        it++ ;
        m_bytes = *(it) ;
        it++; it++; it++ ;
        m_protocol = *(it) ;
        m_address = *(++it) ;
        m_port = *(++it) ;
        it++ ;  
        string t = *(++it) + ":" + *(++it) + ":" + *(++it) + "." + *(++it) ;
        m_time = t.substr(0, t.size()-2);
    }

    SipMsgData_t::SipMsgData_t( msg_t* msg ) : m_source("network") {
        su_time_t now = su_now() ;
        unsigned short second, minute, hour;
        char time[64] ;
        tport_t *tport = nta_incoming_transport(theOneAndOnlyController->getAgent(), NULL, msg) ;        
        assert(NULL != tport) ;

        second = (unsigned short)(now.tv_sec % 60);
        minute = (unsigned short)((now.tv_sec / 60) % 60);
        hour = (unsigned short)((now.tv_sec / 3600) % 24);
        sprintf(time, "%02u:%02u:%02u.%06lu", hour, minute, second, now.tv_usec) ;
 
        m_time.assign( time ) ;
        if( tport_is_udp(tport ) ) m_protocol = "udp" ;
        else if( tport_is_tcp( tport)  ) m_protocol = "tcp" ;
        else if( tport_has_tls( tport ) ) m_protocol = "tls" ;

        tport_unref( tport ) ;

        init( msg ) ;
    }
    SipMsgData_t::SipMsgData_t( msg_t* msg, nta_incoming_t* irq, const char* source ) : m_source(source) {
        su_time_t now = su_now() ;
        unsigned short second, minute, hour;
        char time[64] ;
        tport_t *tport = nta_incoming_transport(theOneAndOnlyController->getAgent(), irq, msg) ;  

        second = (unsigned short)(now.tv_sec % 60);
        minute = (unsigned short)((now.tv_sec / 60) % 60);
        hour = (unsigned short)((now.tv_sec / 3600) % 24);
        sprintf(time, "%02u:%02u:%02u.%06lu", hour, minute, second, now.tv_usec) ;
 
        m_time.assign( time ) ;
        if( tport_is_udp(tport ) ) m_protocol = "udp" ;
        else if( tport_is_tcp( tport)  ) m_protocol = "tcp" ;
        else if( tport_has_tls( tport ) ) m_protocol = "tls" ;

        tport_unref( tport ) ;

        init( msg ) ;
    }
    SipMsgData_t::SipMsgData_t( msg_t* msg, nta_outgoing_t* orq, const char* source ) : m_source(source) {
        su_time_t now = su_now() ;
        unsigned short second, minute, hour;
        char time[64] ;
        tport_t *tport = nta_outgoing_transport( orq ) ;

        second = (unsigned short)(now.tv_sec % 60);
        minute = (unsigned short)((now.tv_sec / 60) % 60);
        hour = (unsigned short)((now.tv_sec / 3600) % 24);
        sprintf(time, "%02u:%02u:%02u.%06lu", hour, minute, second, now.tv_usec) ;
 
        m_time.assign( time ) ;

        if( tport_is_udp(tport ) ) m_protocol = "udp" ;
        else if( tport_is_tcp( tport)  ) m_protocol = "tcp" ;
        else if( tport_has_tls( tport ) ) m_protocol = "tls" ;

        init( msg ) ;

        if( 0 == strcmp(source, "application") ) {
            if( NULL != tport ) {
                const tp_name_t* name = tport_name(tport) ;
                m_address = name->tpn_host ;
                m_port = name->tpn_port ;                
            }
            else {
                m_address = theOneAndOnlyController->getMySipAddress() ;
                m_port = theOneAndOnlyController->getMySipPort() ;
            }
        }

        tport_unref( tport ) ;

    }
    void SipMsgData_t::init( msg_t* msg ) {
        su_sockaddr_t const *su = msg_addr(msg);
        short port ;
        char name[SU_ADDRSIZE] = "";
        char szTmp[10] ;

        su_inet_ntop(su->su_family, SU_ADDR(su), name, sizeof(name));

        m_address.assign( name ) ;
        sprintf( szTmp, "%u", ntohs(su->su_port) ) ;
        m_port.assign( szTmp );
        sprintf( szTmp, "%u", msg_size( msg ) ) ;
        m_bytes.assign( szTmp ) ;
    }
 }

