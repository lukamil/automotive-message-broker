/*
Copyright (C) 2012 Intel Corporation

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "abstractdbusinterface.h"
#include "debugout.h"
#include <boost/algorithm/string.hpp>
#include <gio/gio.h>

#include "listplusplus.h"
#include "abstractproperty.h"

unordered_map<string, AbstractDBusInterface*> AbstractDBusInterface::interfaceMap;
list<string> AbstractDBusInterface::mimplementedProperties;


static void handleMethodCall(GDBusConnection       *connection,
							 const gchar           *sender,
							 const gchar           *object_path,
							 const gchar           *interface_name,
							 const gchar           *method_name,
							 GVariant              *parameters,
							 GDBusMethodInvocation *invocation,
							 gpointer               user_data)
{

	std::string method = method_name;



}

AbstractDBusInterface::AbstractDBusInterface(string interfaceName, string op,
											 GDBusConnection* connection)
	: mInterfaceName(interfaceName), mObjectPath(op), mConnection(connection)
{
	interfaceMap[interfaceName] = this;
	startRegistration();
}

void AbstractDBusInterface::addProperty(AbstractProperty* property)
{
	string nameToLower = property->name();
	boost::algorithm::to_lower<string>(nameToLower);
	
	string access;

	if(property->access() == AbstractProperty::Read)
		access = "read";
	else if(property->access() == AbstractProperty::Write)
		access = "write";
	else if(property->access() == AbstractProperty::ReadWrite)
		access = "readwrite";
	else throw -1; //FIXME: don't throw

	///see which properties are supported:
	introspectionXml += 	"<property type='"+ property->signature() + "' name='"+ property->name()+"' access='"+access+"' />"
			"<method name='get" + property->name() + "Extended'>"
			"	<arg type='v' direction='out' name='value' />"
			"	<arg type='d' direction='out' name='timestamp' />"
			"	<arg type='d' direction='out' name='sequence' />"
			"</method>"
			"<signal name='" + property->name() + "Changed' >"
			"	<arg type='v' name='" + nameToLower + "' direction='out' />"
			"	<arg type='d' name='timestamp' direction='out' />"
			"</signal>";
	
	properties[property->name()] = property;

	if(!ListPlusPlus<string>(&mimplementedProperties).contains(property->name()))
	{
		mimplementedProperties.push_back(property->name());
	}
}

void AbstractDBusInterface::registerObject()
{
	if(!mConnection)
	{
		throw std::runtime_error("forgot to call setDBusConnection on AbstractDBusInterface");
	}
	
	if(introspectionXml.empty())
	{
		cerr<<"no interface to export: "<<mInterfaceName<<endl;
		throw -1;
	}

	introspectionXml += "</interface>";
	introspectionXml += "</node>";
	
	GError* error=NULL;

	GDBusNodeInfo* introspection = g_dbus_node_info_new_for_xml(introspectionXml.c_str(), &error);
	
	if(!introspection || error)
	{

		DebugOut(0)<<"Error in "<<__FILE__<<" - "<<__FUNCTION__<<":"<<__LINE__<<endl;
		DebugOut(0)<<error->message<<endl;
		DebugOut(0)<<"probably bad xml:"<<endl;
		DebugOut(0)<<introspectionXml<<endl;

		g_error_free(error);

		return;
	}

	GDBusInterfaceInfo* mInterfaceInfo = g_dbus_node_info_lookup_interface(introspection, mInterfaceName.c_str());


	const GDBusInterfaceVTable vtable = { handleMethodCall, AbstractDBusInterface::getProperty, AbstractDBusInterface::setProperty };

	regId = g_dbus_connection_register_object(mConnection, mObjectPath.c_str(), mInterfaceInfo, &vtable, this, NULL, &error);
	
	if(error) throw -1;
	
	g_assert(regId > 0);
}

void AbstractDBusInterface::unregisterObject()
{
	g_dbus_connection_unregister_object(mConnection, regId);
}

void AbstractDBusInterface::updateValue(AbstractProperty *property)
{
	if(mConnection == nullptr)
	{
		return;
	}

	GError *error = NULL;

	GVariant **params = g_new(GVariant*,2);
	GVariant *val = g_variant_ref(property->toGVariant());
	params[0] = g_variant_new("v",val);
	params[1] = g_variant_new("d",property->timestamp());

	GVariant *tuple_variant = g_variant_new_tuple(params,2);

	g_dbus_connection_emit_signal(mConnection, NULL, mObjectPath.c_str(), mInterfaceName.c_str(), string(property->name() + "Changed").c_str(), tuple_variant, &error);

	g_free(params);
	g_variant_unref(val);

	if(error)
	{
		DebugOut(0)<<error->message<<endl;
		//throw -1;
	}
}

AbstractDBusInterface *AbstractDBusInterface::getInterfaceForProperty(string property)
{
	for(auto itr = interfaceMap.begin(); itr != interfaceMap.end(); itr++)
	{
		auto interface = (*itr).second;
		if(interface->implementsProperty(property))
			return interface;
	}

	return NULL;
}

bool AbstractDBusInterface::implementsProperty(string property)
{
	for(auto itr = properties.begin(); itr != properties.end(); itr++)
	{
		if((*itr).first == property)
		{
			return true;
		}
	}

	return false;
}

void AbstractDBusInterface::startRegistration()
{
	unregisterObject();
	introspectionXml ="<node>" ;
	introspectionXml += "<interface name='"+ mInterfaceName + "' >";
}

GVariant* AbstractDBusInterface::getProperty(GDBusConnection* connection, const gchar* sender, const gchar* objectPath, const gchar* interfaceName, const gchar* propertyName, GError** error, gpointer userData)
{
	if(interfaceMap.count(interfaceName))
	{
		GVariant* value = interfaceMap[interfaceName]->getProperty(propertyName);
		return value;

	}
	debugOut("No interface for" + string(interfaceName));
	return nullptr;
}

gboolean AbstractDBusInterface::setProperty(GDBusConnection* connection, const gchar* sender, const gchar* objectPath, const gchar* interfaceName, const gchar* propertyName, GVariant* value, GError** error, gpointer userData)
{
	if(interfaceMap.count(interfaceName))
	{
		interfaceMap[interfaceName]->setProperty(propertyName, value);
		return true;
	}

	return false;
}

void AbstractDBusInterface::setProperty(string propertyName, GVariant *value)
{
	if(properties.count(propertyName))
	{
		properties[propertyName]->fromGVariant(value);
	}
	else
	{
		throw -1;
	}
}

GVariant *AbstractDBusInterface::getProperty(string propertyName)
{
	if(properties.count(propertyName))
		return properties[propertyName]->toGVariant();
	else
		throw -1;
}

